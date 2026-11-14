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
            case AP::mxPhraseTrimF: case AP::mxLowPolarityF: case AP::mxKickSectionF: case AP::mxLfHeadroomF: case AP::mxLayerRolesF: case AP::mxPanLayoutF:
                return true;
            default:
                return false;
        }
    }
    inline int featureOf (const Check& c)
    {
        return c.action.startsWith ("autopilot:") ? c.action.fromFirstOccurrenceOf (":", false, false).getIntValue() : -1;
    }
    /** A check can name several switches ("autopilot:a,b,c": the first is the one Ouroboros runs,
        the rest live in the plugins and are told they're needed). */
    inline std::vector<int> featuresOf (const Check& c)
    {
        std::vector<int> v;
        if (! c.action.startsWith ("autopilot:")) return v;
        for (const auto& t : juce::StringArray::fromTokens (c.action.fromFirstOccurrenceOf (":", false, false), ",", ""))
            if (t.trim().isNotEmpty()) v.push_back (t.trim().getIntValue());
        return v;
    }
    /** The track a fix message is for (the low 12 bits of its Quintessence's id), 0 = every track. */
    inline uint32_t targetOf (suite::Fix fix, uint32_t data)
    {
        switch (fix)
        {
            case suite::Fix::MasterTone: case suite::Fix::TrackPunch: case suite::Fix::SectionTone: case suite::Fix::SectionTrim: case suite::Fix::PhraseTrim:
            case suite::Fix::TrackPan: case suite::Fix::TrackPolarity: case suite::Fix::Narrower: case suite::Fix::SectionWidth: case suite::Fix::VocalDelay:
                return data & 0xfffu;
            default: return 0;
        }
    }
    /** A tone move's size and direction (dB), 0 when it isn't one. */
    inline float toneOf (suite::Fix fix, uint32_t data, int& band)
    {
        band = -1;
        if (fix == suite::Fix::MasterTone) { uint32_t l; float db; suite::unpackMasterTone (data, l, band, db); return db; }
        if (fix == suite::Fix::SectionTone) { uint32_t l; int t; float db; suite::unpackSectionTone (data, l, t, band, db); return db; }
        return 0.0f;
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
        static std::array<uint32_t, 8> needsFrom (const Report& rep)
        {
            std::array<uint32_t, 8> m {};
            for (const auto& c : rep.checks)
                if (c.measured && c.score < 0.7f)
                    for (const auto f : featuresOf (c)) if (f >= 0 && f < 256) m[(size_t) (f / 32)] |= 1u << (f % 32);
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
                    history.push_back ({ trying->check, trying->area, trying->seqs, trying->target, trying->band, trying->toneDb, now });
                    if (trying->target != 0)
                    {
                        toneSpent[trying->target] += std::abs (trying->toneDb);
                        if (AP::on (AP::mxProvenanceF)) provenance[trying->target].add (trying->check);
                    }
                    recent.push_back (true);
                    if (AP::on (AP::learnKeptF)) learning.record (trying->check, true);
                    note = trying->check + ": kept (" + juce::String (juce::roundToInt (trying->before * 100.0f)) + " -> " + juce::String (juce::roundToInt (juce::jmax (0.0f, after) * 100.0f)) + ")";
                }
                suite::MoveLog::add ("Ouroboros", {}, "Label-ready autopilot: " + note, suite::MoveLog::fix);
                trying.reset();
                return;
            }
            if (! playing) { wasPlaying = false; return; }
            if (! wasPlaying) { wasPlaying = true; triedThisPlay = 0; }   // a new play-through
            // rollback by area: an area that fell well under where it was at the last milestone has
            // its fixes since then undone (newest first)
            if (AP::on (AP::mxRollbackAreaF) && ! areaAtMilestone.empty())
                for (const auto& [area, was] : areaAtMilestone)
                    if (const auto nowScore = areaScore (rep, area); nowScore >= 0.0f && nowScore < was - 0.1f && rollbackArea (area) > 0)
                    {
                        note = "an area fell since the milestone: its fixes undone";
                        suite::MoveLog::add ("Ouroboros", {}, "Label-ready autopilot: " + note, suite::MoveLog::fix);
                        areaAtMilestone[area] = nowScore;   // (not again for the same fall)
                        return;
                    }
            // the step budget: at most twelve fixes tried per play-through
            if (AP::on (AP::mxStepBudgetF) && triedThisPlay >= 12) { note = "this play-through's fixes are done: play it again for more"; return; }
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
                if (const auto tgt = targetOf (c.fix, c.fixData); tgt != 0)
                {
                    int band = -1;
                    const auto tone = toneOf (c.fix, c.fixData, band);
                    // the tone budget: no track moved more than 6 dB in all by the autopilot's tone steps
                    if (AP::on (AP::mxToneBudgetF) && std::abs (tone) > 0.0f && toneSpent[tgt] >= 6.0f) continue;
                    // arbitration: not the opposite of a move kept on the same track and band in the last ten minutes
                    if (AP::on (AP::mxArbitrationF) && std::abs (tone) > 0.0f && fights (tgt, band, tone, now)) continue;
                }
                // fixes you've kept before go first
                const auto rank = c.score - (AP::on (AP::learnKeptF) && learning.favoured (c.name) ? 0.3f : 0.0f);
                const auto pickRank = pick == nullptr ? 2.0f : pick->score - (AP::on (AP::learnKeptF) && learning.favoured (pick->name) ? 0.3f : 0.0f);
                if (rank < pickRank) pick = &c;
            }
            if (pick == nullptr) { note = "nothing to do"; return; }
            Attempt a;
            a.check = pick->name; a.before = pick->score; a.area = pick->area;
            a.target = targetOf (pick->fix, pick->fixData); a.toneDb = toneOf (pick->fix, pick->fixData, a.band);
            ++triedThisPlay;
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

        void reset() { trying.reset(); steps.clear(); resting.clear(); note = {}; kept = undone = 0; recent.clear(); history.clear(); toneSpent.clear(); provenance.clear(); areaAtMilestone.clear(); triedThisPlay = 0; }

        /** A milestone: each area's score now, for rolling back an area that falls; the fixes so far are kept for good. */
        void milestone (const Report& rep)
        {
            areaAtMilestone.clear();
            for (const auto& c : rep.checks) if (c.measured) areaAtMilestone[c.area] = 0.0f;
            for (auto& [area, v] : areaAtMilestone) v = areaScore (rep, area);
            history.clear();
        }
        /** Undo the fixes kept in one area since the last milestone (newest first). Returns how many. */
        int rollbackArea (int area)
        {
            int n = 0;
            for (auto it = history.rbegin(); it != history.rend(); ++it)
                if (it->area == area && ! it->seqs.empty())
                {
                    for (auto s : it->seqs) suite::FixBus::sendUndo (s);
                    if (it->target != 0) toneSpent[it->target] = juce::jmax (0.0f, toneSpent[it->target] - std::abs (it->toneDb));
                    it->seqs.clear(); ++n;
                }
            return n;
        }
        /** Who changed each track: the checks whose fixes were kept on it (low 12 bits of its id). */
        juce::StringArray provenanceOf (uint32_t id) const
        {
            const auto it = provenance.find (id & 0xfffu);
            return it != provenance.end() ? it->second : juce::StringArray();
        }
        float toneSpentOn (uint32_t id) const { const auto it = toneSpent.find (id & 0xfffu); return it != toneSpent.end() ? it->second : 0.0f; }
        int triedThisPlayThrough() const { return triedThisPlay; }
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
        struct Attempt { juce::String check; float before = 0.0f; int updates = 0; std::vector<uint32_t> seqs; int area = 0; uint32_t target = 0; int band = -1; float toneDb = 0.0f; };
        struct Kept { juce::String check; int area = 0; std::vector<uint32_t> seqs; uint32_t target = 0; int band = -1; float toneDb = 0.0f; juce::int64 at = 0; };
        std::vector<Kept> history;   // since the last milestone
        std::map<uint32_t, float> toneSpent;
        std::map<uint32_t, juce::StringArray> provenance;
        std::map<int, float> areaAtMilestone;
        int triedThisPlay = 0;
        bool wasPlaying = false;
        bool fights (uint32_t target, int band, float tone, juce::int64 now) const
        {
            for (const auto& k : history)
                if (k.target == target && k.band == band && now - k.at < 10 * 60 * 1000 && k.toneDb * tone < 0.0f && ! k.seqs.empty()) return true;
            return false;
        }
        static float areaScore (const Report& rep, int area)
        {
            float sum = 0.0f; int n = 0;
            for (const auto& c : rep.checks) if (c.area == area && c.measured) { sum += c.score; ++n; }
            return n > 0 ? sum / (float) n : -1.0f;
        }
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
