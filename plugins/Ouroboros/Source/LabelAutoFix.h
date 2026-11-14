#pragma once

// The label-ready autopilot: the automatic fixes Ouroboros runs itself. Each new check names the
// autopilot switch that fixes it ("autopilot:<feature>"). Two kinds:
//  - fixes that live in a plugin (pad motion, the breakdown low cut, input staging, vocal timing,
//    idle plugins sleeping, the bass and arp clean-ups in Correspondence's parts...): Ouroboros
//    tells them where they're needed (LabelReadyBus::publishNeeds) and they act, latched, while
//    their switch is on;
//  - fixes Ouroboros sends (a tone step on one track, a level in one kind of section, less
//    squash, the reverbs' low cut, a word to the director...): one at a time, the check listened
//    to for a few seconds, kept if it moved up, undone (and rested) if it didn't, at most three
//    steps per check.

#include "LabelReady.h"
#include "LabelReadyBus.h"
#include "FixBus.h"
#include "MoveLog.h"
#include "LabelLearning.h"
#include <map>
#include <optional>
#include <set>

namespace lab
{
    namespace AP = suite::Autopilot;

    /** Fixes Ouroboros sends itself (the rest live in the plugins). */
    inline bool sentByOuroboros (int f)
    {
        switch (f)
        {
            case AP::kickShapeF: case AP::leadTimbreF: case AP::clapTransientF: case AP::hygieneF: case AP::sectionFxF: case AP::phraseFillsF:
            case AP::preDropCurveF: case AP::vocalClarityF: case AP::sibilanceF: case AP::vocalSteadyF: case AP::throwsF: case AP::squashF:
            case AP::peakTrimF: case AP::stressFixF: case AP::reverbLowsF: case AP::monoDoublesF: case AP::hatsToneF: case AP::harshAttackF:
            case AP::fxUnderLeadF: case AP::preDelayF: case AP::widthBuildF: case AP::contrastF: case AP::vocalTimingF:
            case AP::roleTimbreF: case AP::journeyF: case AP::sectionWidthF: case AP::phraseDynamicsF: case AP::kickBassPairF: case AP::harshExposureF:
            case AP::subPhaseF: case AP::airF: case AP::riserTopF: case AP::sectionEdgesF: case AP::breakdownSwellF: case AP::layerSplitF: case AP::subFillF:
            case AP::spaceSceneF: case AP::sectionToneF: case AP::depthF: case AP::echoTighterF: case AP::reverbDuckF: case AP::earCandySynthF:
                return true;
            default:
                return false;
        }
    }
    inline int featureOf (const Check& c)
    {
        return c.action.startsWith ("autopilot:") ? c.action.fromFirstOccurrenceOf (":", false, false).getIntValue() : -1;
    }

    class AutoFixer
    {
    public:
        /** Says something to the director (the processor's chat), true if it did something. */
        std::function<bool (const juce::String&)> director;
        /** Writes the ear-candy one-shots (the host), true if it wrote any. */
        std::function<bool()> earCandy;
        Learning learning;   // what you kept and undid, across songs
        juce::String note;                              // what it's doing (the dashboard)
        int kept = 0, undone = 0;

        /** The plugins' fixes: which switches are needed now (a check under 70%). */
        static std::array<uint32_t, 4> needsFrom (const Report& rep)
        {
            std::array<uint32_t, 4> m {};
            for (const auto& c : rep.checks)
                if (const auto f = featureOf (c); f >= 0 && f < 128 && c.measured && c.score < 0.7f) m[(size_t) (f / 32)] |= 1u << (f % 32);
            return m;
        }

        /** One step (each update, while the song plays). */
        void step (const Report& rep, const std::vector<suite::TrackTraits::Traits>& traits, bool playing)
        {
            const auto now = juce::Time::currentTimeMillis();
            if (trying)
            {
                if (! playing || ++trying->updates < 4) return;   // listen for about eight seconds of the song
                const auto after = scoreOf (rep, trying->check);
                if (after >= 0.0f && after < trying->before + 0.03f && ! trying->seqs.empty())
                {
                    for (auto s : trying->seqs) suite::FixBus::sendUndo (s);
                    resting[trying->check] = now + 10 * 60 * 1000;
                    ++undone;
                    recent.push_back (false);
                    if (AP::on (AP::learnUndoneF)) learning.record (trying->check, false);
                    note = trying->check + ": no better, undone";
                }
                else
                {
                    ++steps[trying->check];
                    ++kept;
                    recent.push_back (true);
                    if (AP::on (AP::learnKeptF)) learning.record (trying->check, true);
                    note = trying->check + ": kept (" + juce::String (juce::roundToInt (trying->before * 100.0f)) + " -> " + juce::String (juce::roundToInt (juce::jmax (0.0f, after) * 100.0f)) + ")";
                }
                suite::MoveLog::add ("Ouroboros", {}, "Label-ready autopilot: " + note, suite::MoveLog::fix);
                trying.reset();
                return;
            }
            if (! playing) return;
            // pacing: several undone in a row, wait longer between tries
            while (recent.size() > 5) recent.erase (recent.begin());
            int failed = 0; for (auto k : recent) failed += k ? 0 : 1;
            if (AP::on (AP::pacingF) && failed >= 3 && ++idleSteps % 3 != 0) { note = "pausing: the last fixes didn't help"; return; }
            const Check* pick = nullptr;
            for (const auto& c : rep.checks)
            {
                const auto f = featureOf (c);
                if (f < 0 || ! sentByOuroboros (f) || ! AP::on ((AP::Feature) f) || ! c.measured || c.score >= 0.75f) continue;
                if (steps[c.name] >= 3 || (resting.count (c.name) && resting[c.name] > now)) continue;
                if (AP::on (AP::learnUndoneF) && learning.retired (c.name)) continue;
                // fixes you've kept before go first
                const auto rank = c.score - (AP::on (AP::learnKeptF) && learning.favoured (c.name) ? 0.3f : 0.0f);
                const auto pickRank = pick == nullptr ? 2.0f : pick->score - (AP::on (AP::learnKeptF) && learning.favoured (pick->name) ? 0.3f : 0.0f);
                if (rank < pickRank) pick = &c;
            }
            if (pick == nullptr) { note = "nothing to do"; return; }
            Attempt a;
            a.check = pick->name; a.before = pick->score;
            a.seqs = apply (*pick, traits);
            if (a.seqs.empty() && ! appliedOther)
            {
                steps[pick->name] = 3;   // nothing it can send for that one
                return;
            }
            if (a.seqs.empty()) { steps[pick->name] = 3; note = pick->name + ": done (" + otherWhat + ")"; return; }   // director and map moves aren't undone
            note = "trying a fix for " + pick->name;
            trying = a;
        }

        /** The check's fix, sent now (also "Fix this" on a check whose switch Ouroboros runs). Returns
            the fix messages sent (to undo); director moves aren't messages (appliedOther). */
        std::vector<uint32_t> apply (const Check& c, const std::vector<suite::TrackTraits::Traits>& traits)
        {
            appliedOther = false; otherWhat = {};
            std::vector<uint32_t> seqs;
            const auto f = featureOf (c);
            if (f == AP::journeyF || f == AP::sectionWidthF)
            {
                // every track (but the kick and bass, for width) a step in that kind of section
                uint32_t low; int type; float amount;
                suite::unpackSectionTrim (c.fixData, low, type, amount);
                for (const auto& t : traits)
                    if (t.role != 0 && (f == AP::journeyF || (t.role != 1 && t.role != 2)))
                        seqs.push_back (suite::FixBus::send (c.fix, suite::packSectionTrim (t.id, type, amount)));
                return seqs;
            }
            if (f == AP::earCandySynthF)
            {
                if (earCandy && earCandy()) { appliedOther = true; otherWhat = "ear-candy one-shots written"; }
                return seqs;
            }
            if (f == AP::contrastF)
            {
                // the breakdowns a step quieter on every track (Quintessence), so the drops land harder
                for (const auto& t : traits) if (t.role != 0) seqs.push_back (suite::FixBus::send (suite::Fix::SectionTrim, suite::packSectionTrim (t.id, (int) T::Breakdown, -1.0f)));
                return seqs;
            }
            if (c.fix != suite::Fix::None) { seqs.push_back (suite::FixBus::send (c.fix, scaled (c, c.fixData))); return seqs; }
            juce::String words;
            if (f == AP::sectionFxF || f == AP::phraseFillsF || f == AP::sectionEdgesF) words = "busier";
            else if (f == AP::widthBuildF) words = "wider drop";
            else if (f == AP::preDropCurveF) words = "bigger build";
            else if (f == AP::breakdownSwellF) words = "busier breakdown";
            else if (f == AP::spaceSceneF) words = "drier drop";
            if (words.isNotEmpty() && director != nullptr && director (words)) { appliedOther = true; otherWhat = "the director: " + words; }
            if (f == AP::spaceSceneF && appliedOther && director != nullptr) { director ("more space breakdown"); otherWhat << ", more space breakdown"; }
            return seqs;
        }

        void reset() { trying.reset(); steps.clear(); resting.clear(); note = {}; kept = undone = 0; recent.clear(); }
        /** A fix's amount, smaller each time you undid that check before (learning). */
        uint32_t scaled (const Check& c, uint32_t data)
        {
            if (! AP::on (AP::learnUndoneF)) return data;
            const auto k = learning.stepScale (c.name);
            if (k >= 0.99f) return data;
            if (c.fix == suite::Fix::MasterTone || c.fix == suite::Fix::TrackPunch)
            {
                uint32_t low; int band; float db;
                suite::unpackMasterTone (data, low, band, db);
                return suite::packMasterTone (low, band, db * k);
            }
            if (c.fix == suite::Fix::SectionTrim)
            {
                uint32_t low; int type; float db;
                suite::unpackSectionTrim (data, low, type, db);
                return suite::packSectionTrim (low, type, db * k);
            }
            return data;
        }

    private:
        struct Attempt { juce::String check; float before = 0.0f; int updates = 0; std::vector<uint32_t> seqs; };
        std::optional<Attempt> trying;
        std::map<juce::String, int> steps;
        std::map<juce::String, juce::int64> resting;
        bool appliedOther = false;
        std::vector<bool> recent;
        int idleSteps = 0;
        juce::String otherWhat;
        static float scoreOf (const Report& rep, const juce::String& name)
        {
            for (const auto& c : rep.checks) if (c.name == name && c.measured) return c.score;
            return -1.0f;
        }
    };
}
