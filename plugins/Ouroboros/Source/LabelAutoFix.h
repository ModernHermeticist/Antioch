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
#include "LabelPluginJudge.h"
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
            case AP::mx2LaterDropToneF: case AP::mx2ClapLayersF:
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
        static suite::LabelReadyBus::Needs needsFrom (const Report& rep)
        {
            suite::LabelReadyBus::Needs m {};
            for (const auto& c : rep.checks)
                if (c.measured && c.score < 0.7f)
                    for (const auto f : featuresOf (c)) if (f >= 0 && f < 384) m[(size_t) (f / 32)] |= 1u << (f % 32);
            return m;
        }

        /** What the host knows about this update. */
        struct Context
        {
            bool judgeable = true;    // a real play-through: not a loop, not soloed or muted down
            bool settling = false;    // a plugin just switched a fix, or a Quintessence is mid-pass
            bool rendering = false;   // FL is rendering: nothing changes
            int sectionType = -1;     // where the playhead is
        };
        /** Each area: 0 the autopilot fixes it, 1 it proposes (you approve), 2 off. */
        std::array<int, numAreas> areaMode {};
        struct Proposal { juce::String check; int area = 0; juce::String detail; };
        std::vector<Proposal> proposals;
        std::set<juce::String> approved;
        CheckNoise noise;
        /** The plateau: nothing moved for three play-throughs; what's left that only you can fix. */
        bool plateau = false;
        juce::StringArray plateauReport;
        bool allowedByMode (const Check& c) const
        {
            const auto m = c.area >= 0 && c.area < numAreas ? areaMode[(size_t) c.area] : 0;
            return m == 0 || (m == 1 && approved.count (c.name) > 0);
        }

        /** One step (each update, while the song plays). */
        void step (const Report& rep, const std::vector<suite::TrackTraits::Traits>& traits, bool playing) { step (rep, traits, playing, Context()); }
        void step (const Report& rep, const std::vector<suite::TrackTraits::Traits>& traits, bool playing, const Context& cx)
        {
            const auto now = juce::Time::currentTimeMillis();
            if (cx.rendering && AP::on (AP::mx2RenderFreezeF)) { note = "FL is rendering: nothing changes"; return; }
            playThroughs (rep, playing, cx);
            if (trying)
            {
                if (! playing) return;
                if (cx.settling && AP::on (AP::mx2SettleF)) { note = "waiting for the plugins to settle"; return; }
                if (! cx.judgeable && AP::on (AP::mx2ValidPlaysF)) { note = "not judging: a loop, a solo or a short play"; return; }
                // judged where it acts: a fix for one kind of section is listened to in that kind of section
                if (AP::on (AP::mx2JudgeWhereF) && trying->sectionType >= 0 && cx.sectionType >= 0 && cx.sectionType != trying->sectionType) return;
                if (++trying->updates < 4) return;   // listen for about eight seconds of the song
                const auto after = scoreOf (rep, trying->check);
                if (after >= 0.0f && after < trying->before + noise.of (trying->check) && ! trying->seqs.empty())
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
                    history.push_back ({ trying->check, trying->area, trying->seqs, trying->target, trying->band, trying->toneDb, now, trying->before, 0, playId, false });
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
            if (! playing) return;
            // confirmation: a fix kept in one play-through is checked again in the next; one that doesn't hold is undone
            if (AP::on (AP::mx2ConfirmF) && cx.judgeable && ! cx.settling)
                for (auto& k : history)
                    if (! k.confirmed && ! k.seqs.empty() && k.playId != playId && ++k.confirmUpdates >= 4)
                    {
                        const auto sc = scoreOf (rep, k.check);
                        if (sc >= 0.0f && sc < k.before + noise.of (k.check))
                        {
                            for (auto q : k.seqs) suite::FixBus::sendUndo (q);
                            if (k.target != 0) toneSpent[k.target] = juce::jmax (0.0f, toneSpent[k.target] - std::abs (k.toneDb));
                            k.seqs.clear(); ++undone;
                            note = k.check + ": didn't hold in a second play-through, undone";
                            suite::MoveLog::add ("Ouroboros", {}, "Label-ready autopilot: " + note, suite::MoveLog::fix);
                            return;
                        }
                        k.confirmed = true;
                    }
            if (plateau && AP::on (AP::mx2PlateauF)) { note = "nothing's moved for three play-throughs: see what's left for you"; return; }
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
            float pickRank = 99.0f;
            for (const auto& c : rep.checks)
            {
                const auto f = featureOf (c);
                if (f < 0 || ! sentByOuroboros (f) || ! AP::on ((AP::Feature) f) || ! c.measured || c.score >= 0.75f) continue;
                // propose mode: the area's fixes wait for you
                if (! allowedByMode (c))
                {
                    if (c.area >= 0 && c.area < numAreas && areaMode[(size_t) c.area] == 1)
                    {
                        bool have = false; for (const auto& p : proposals) have = have || p.check == c.name;
                        if (! have) proposals.push_back ({ c.name, c.area, c.detail });
                    }
                    continue;
                }
                if (steps[c.name] >= 3 || (resting.count (c.name) && resting[c.name] > now)) continue;
                if (AP::on (AP::mx2ConfirmF) && awaitingConfirmation (c.name)) continue;   // its last fix isn't confirmed yet
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
                // fixes you've kept before go first; tone before level before stereo; the label's heaviest areas first
                auto rank = c.score - (AP::on (AP::learnKeptF) && learning.favoured (c.name) ? 0.3f : 0.0f);
                if (AP::on (AP::mx2FixOrderF)) rank += 2.0f * (float) categoryOf (c.fix);
                if (AP::on (AP::mx2AreaPriorityF) && c.area >= 0 && c.area < numAreas)
                {
                    const auto as = rep.areaMeasured[(size_t) c.area] ? rep.areaScore[(size_t) c.area] : 1.0f;
                    rank -= 0.5f * areaWeight (c.area) / 20.0f * (1.0f - as);
                }
                if (rank < pickRank) { pickRank = rank; pick = &c; }
            }
            if (pick == nullptr) { note = "nothing to do"; return; }
            Attempt a;
            a.check = pick->name; a.before = pick->score; a.area = pick->area;
            a.target = targetOf (pick->fix, pick->fixData); a.toneDb = toneOf (pick->fix, pick->fixData, a.band);
            a.sectionType = sectionOf (pick->fix, pick->fixData);
            touchedThisPlay.insert (pick->name);
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

        void reset() { trying.reset(); steps.clear(); resting.clear(); note = {}; kept = undone = 0; recent.clear(); history.clear(); toneSpent.clear(); provenance.clear(); areaAtMilestone.clear(); triedThisPlay = 0;
                       proposals.clear(); approved.clear(); noise.reset(); plateau = false; plateauReport.clear(); playEnds.clear(); }
        /** Approve a proposal: tried at the next step. Reject: left alone for this song. */
        void approve (const juce::String& check) { approved.insert (check); proposals.erase (std::remove_if (proposals.begin(), proposals.end(), [&] (const Proposal& p) { return p.check == check; }), proposals.end()); }
        void reject (const juce::String& check) { steps[check] = 3; proposals.erase (std::remove_if (proposals.begin(), proposals.end(), [&] (const Proposal& p) { return p.check == check; }), proposals.end()); }
        /** Tone fixes first (0), then level (1), then stereo (2). */
        static int categoryOf (suite::Fix f)
        {
            switch (f)
            {
                case suite::Fix::MasterTone: case suite::Fix::SectionTone: case suite::Fix::TrackPunch: case suite::Fix::ReverbLowCut: case suite::Fix::DriveSofter: return 0;
                case suite::Fix::TrackPan: case suite::Fix::Narrower: case suite::Fix::SectionWidth: case suite::Fix::TrackPolarity: return 2;
                default: return 1;
            }
        }
        /** The kind of section a fix acts in (-1 = everywhere). */
        static int sectionOf (suite::Fix f, uint32_t data)
        {
            uint32_t low; int type = -1, band; float db;
            if (f == suite::Fix::SectionTrim || f == suite::Fix::SectionWidth) suite::unpackSectionTrim (data, low, type, db);
            else if (f == suite::Fix::SectionTone) suite::unpackSectionTone (data, low, type, band, db);
            return type >= 0 && type < 6 ? type : -1;
        }

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
        struct Attempt { juce::String check; float before = 0.0f; int updates = 0; std::vector<uint32_t> seqs; int area = 0; uint32_t target = 0; int band = -1; float toneDb = 0.0f; int sectionType = -1; };
        struct Kept { juce::String check; int area = 0; std::vector<uint32_t> seqs; uint32_t target = 0; int band = -1; float toneDb = 0.0f; juce::int64 at = 0;
                      float before = 0.0f; int confirmUpdates = 0; int playId = 0; bool confirmed = false; int fadedPlays = 0; };
        int playId = 0;
        std::set<juce::String> touchedThisPlay;
        std::vector<int> playEnds;          // the overall score at the end of each play-through
        std::vector<int> keptAtPlayEnd;     // how many fixes had been kept by then
        float lastCoverage = 0.0f;
        bool lastJudgeable = true;
        /** The edges of each play-through: the noise floor learns from plain plays, fading fixes are undone, the plateau found. */
        void playThroughs (const Report& rep, bool playing, const Context& cx)
        {
            if (playing && ! wasPlaying) { wasPlaying = true; triedThisPlay = 0; ++playId; touchedThisPlay.clear(); }
            if (playing) { lastCoverage = rep.coverage; lastJudgeable = lastJudgeable && cx.judgeable; return; }
            if (! wasPlaying) return;
            wasPlaying = false;
            const auto real = lastJudgeable && lastCoverage >= 0.6f;
            lastJudgeable = true;
            if (! real) return;
            noise.playEnded (rep, touchedThisPlay);
            // fixes whose gain has faded: back to where they started for three play-throughs, undone
            if (AP::on (AP::mx2FadeUndoF))
                for (auto& k : history)
                {
                    if (k.seqs.empty()) continue;
                    const auto sc = scoreOf (rep, k.check);
                    k.fadedPlays = sc >= 0.0f && sc < k.before + noise.of (k.check) ? k.fadedPlays + 1 : 0;
                    if (k.fadedPlays >= 3)
                    {
                        for (auto q : k.seqs) suite::FixBus::sendUndo (q);
                        if (k.target != 0) toneSpent[k.target] = juce::jmax (0.0f, toneSpent[k.target] - std::abs (k.toneDb));
                        k.seqs.clear(); ++undone;
                        suite::MoveLog::add ("Ouroboros", {}, "Label-ready autopilot: " + k.check + ": its gain faded, undone", suite::MoveLog::fix);
                    }
                }
            // the plateau: three real play-throughs with the score within a point and nothing new kept
            playEnds.push_back (rep.overall); keptAtPlayEnd.push_back (kept);
            while (playEnds.size() > 3) { playEnds.erase (playEnds.begin()); keptAtPlayEnd.erase (keptAtPlayEnd.begin()); }
            if (playEnds.size() == 3)
            {
                const auto spread = *std::max_element (playEnds.begin(), playEnds.end()) - *std::min_element (playEnds.begin(), playEnds.end());
                const auto nowPlateau = spread <= 1 && keptAtPlayEnd.back() == keptAtPlayEnd.front();
                if (nowPlateau && ! plateau) plateauReport = leftForYou (rep);
                plateau = nowPlateau;
            }
        }
        /** The three things left that only you can fix: no automatic fix, weighted by how much each costs the score. */
        static juce::StringArray leftForYou (const Report& rep)
        {
            std::vector<std::pair<float, const Check*>> v;
            for (const auto& c : rep.checks)
                if (c.measured && c.score < 0.9f && c.fix == suite::Fix::None && ! c.action.startsWith ("autopilot:"))
                    v.push_back ({ areaWeight (c.area) * (1.0f - c.score), &c });
            std::sort (v.begin(), v.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
            juce::StringArray out;
            for (size_t i = 0; i < v.size() && i < 3; ++i)
                out.add (v[i].second->name + ": " + v[i].second->detail + (v[i].second->explain.isNotEmpty() ? " (" + v[i].second->explain + ")" : juce::String())
                         + " - worth about " + juce::String (juce::roundToInt (v[i].first)) + " points");
            return out;
        }
        std::vector<Kept> history;   // since the last milestone
        std::map<uint32_t, float> toneSpent;
        std::map<uint32_t, juce::StringArray> provenance;
        std::map<int, float> areaAtMilestone;
        int triedThisPlay = 0;
        bool wasPlaying = false;
        bool awaitingConfirmation (const juce::String& check) const
        {
            for (const auto& k : history) if (k.check == check && ! k.confirmed && ! k.seqs.empty()) return true;
            return false;
        }
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
