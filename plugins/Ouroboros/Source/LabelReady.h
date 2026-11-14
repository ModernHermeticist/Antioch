#pragma once

// The label-ready checks: is this a track a label signs and can release? Eight areas, each a set
// of checks scored 0..1 against the label profile (or generic trance norms), with what's wrong,
// where (the bar), what moving it would take, and a fix where the suite has one. Pure: it reads a
// snapshot of what the suite measured (the song bar by bar, the sections, the master, every track,
// the written parts, the director's plan), so it runs anywhere and is testable.

#include "LabelProfile.h"
#include "SuiteDirector.h"
#include "SongBus.h"
#include "LabelReadyBus.h"
#include "Registry.h"
#include "FixBus.h"
#include "TrackTraits.h"
#include "AutopilotBus.h"
#include <juce_core/juce_core.h>
#include <map>
#include <set>

namespace lab
{
    enum Area { aReference, aAR, aWriting, aLowEnd, aMids, aHygiene, aTranslation, aBalance, numAreas };
    inline const char* areaName (int a)
    {
        static const char* n[] = { "Against the label", "A&R", "Writing", "Low end", "Mids, top and space", "Hygiene", "Translation and mono", "Balance" };
        return a >= 0 && a < numAreas ? n[a] : "";
    }
    inline float areaWeight (int a) { static const float w[] = { 8, 20, 15, 15, 12, 10, 10, 10 }; return a >= 0 && a < numAreas ? w[a] : 0.0f; }

    struct Check
    {
        int area = aAR;
        juce::String name, detail, explain;   // what it found; what moving it would take
        float score = 1.0f;
        bool measured = true, blocking = false, safeFix = false, belowMinimum = false;
        int bar = -1;                         // where (1-based), -1 = the whole song
        suite::Fix fix = suite::Fix::None;    // a one-click fix the suite can make
        uint32_t fixData = 0;
        juce::String action;                  // or a director / writing action ("director: hit harder", "write stronger")
    };

    struct RoleLevel { int role = 0; juce::String track; float levelDb = -100.0f; Bands bands {}; };

    struct Inputs
    {
        suite::SectionMap map;
        double bpm = 138.0, barQ = 4.0;
        std::vector<Bar> bars;                       // the song, bar by bar (Ouroboros on the master)
        std::vector<SectionStat> sections;
        const Profile* profile = nullptr;
        suite::LabelReadyBus::Master master;
        suite::SongBus::Song song;
        std::vector<suite::SongBus::Note> vocal;     // what the vocal sings (Solve et Coagula)
        std::vector<RoleLevel> tracks;               // every Quintessence: its role and its spectrum now
        std::vector<suite::SuiteEntry> entries;      // the registry
        const suite::director::Plan* plan = nullptr;
        float kickHz = 0.0f;                         // the kick's body (Kybalion), 0 = unknown
        std::map<int, float> timingMs;               // per role: how far its hits sit off the 16th grid (median, ms)
        std::map<int, std::array<float, numTypes>> roleLevelPerType;   // per role, per kind of section: its level (dB)
        std::vector<juce::String> otherHooks;        // earlier songs' hooks (interval prints)
        int fxRepitched = 0, clicksGuarded = 0;
        std::vector<suite::TrackTraits::Traits> traits;  // what each Quintessence measures on its track
        std::vector<juce::String> otherProgressions;     // your earlier songs' drop progressions (prints)
        juce::String groovePrint;                        // this song's kit groove (KitBus), as hits
        std::vector<juce::String> otherGrooves;          // your earlier songs' grooves
        int focusSection = -1;                           // score one section only (its index), -1 = the whole song
        std::vector<float> vocalCents;                   // per sung note (in.vocal): how far off the note (+ sharp)
        std::map<int, float> swingMs;                    // per role: how late its offbeat 16ths sit (ms)
        float flamMs = -1.0f;                            // the bass's start against the kick on shared beats (median ms), -1 = unknown
        std::vector<float> resonanceHz;                  // what each track's resonance finder hears now
    };

    struct Report
    {
        int overall = 0;
        std::array<float, numAreas> areaScore {};
        std::array<bool, numAreas> areaMeasured {};
        std::vector<Check> checks;
        juce::String verdict, nextAction;
        bool ready = false;
        juce::StringArray blockers;
        int closestRef = -1;
        float coverage = 0.0f;   // how much of the song was heard (0..1): a score on half a play-through says so
        float closestDistance = 0.0f;
    };

    namespace detail
    {
        inline float ramp (float v, float good, float bad) { if (std::abs (good - bad) < 1.0e-9f) return v >= good ? 1.0f : 0.0f; return juce::jlimit (0.0f, 1.0f, (v - bad) / (good - bad)); }
        inline float fromOutside (float spreads) { return juce::jlimit (0.0f, 1.0f, 1.0f - 0.4f * spreads); }
        inline juce::String db (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
        inline float powerSumDb (const Bands& b, int from, int to)
        {
            double p = 0.0;
            for (int k = from; k <= to; ++k) p += std::pow (10.0, b[(size_t) juce::jlimit (0, numBands - 1, k)] / 10.0);
            return (float) (10.0 * std::log10 (p + 1.0e-12));
        }
        inline juce::String hookPrint (const std::vector<suite::SongBus::Note>& hook)
        {
            juce::StringArray iv;
            for (size_t i = 1; i < hook.size() && iv.size() < 16; ++i) iv.add (juce::String (hook[i].pitch - hook[i - 1].pitch));
            return iv.joinIntoString (",");
        }
        inline float printSimilarity (const juce::String& a, const juce::String& b)
        {
            const auto x = juce::StringArray::fromTokens (a, ",", ""), y = juce::StringArray::fromTokens (b, ",", "");
            const auto n = juce::jmin (x.size(), y.size());
            if (n < 4) return 0.0f;
            int same = 0;
            for (int i = 0; i < n; ++i) same += x[i] == y[i] ? 1 : 0;
            return (float) same / (float) n;
        }
    }

    inline void extraChecks (const Inputs& in, const Profile& P, std::vector<Check>& out);   // LabelChecksMore.h
    inline void checks3 (const Inputs& in, const Profile& P, std::vector<Check>& out);       // LabelChecks3.h
    inline void checks4 (const Inputs& in, const Profile& P, std::vector<Check>& out);       // LabelChecks4.h
    inline void checks5 (const Inputs& in, const Profile& P, std::vector<Check>& out);       // LabelChecks5.h

    inline Report evaluate (const Inputs& in)
    {
        using namespace detail;
        Report rep;
        static const Profile generic = genericProfile();
        const auto& P = in.profile != nullptr && (! in.profile->empty() || in.profile->drops.valid()) ? *in.profile : generic;
        auto& out = rep.checks;
        auto add = [&out] (int area, const juce::String& name, bool measured, float score, const juce::String& detail, const juce::String& explain = {}, int bar = -1,
                           bool blocking = false, suite::Fix fix = suite::Fix::None, uint32_t data = 0, bool safe = false, const juce::String& action = {})
        {
            Check c;
            c.area = area; c.name = name; c.measured = measured; c.score = juce::jlimit (0.0f, 1.0f, score); c.detail = detail; c.explain = explain;
            c.bar = bar; c.blocking = blocking; c.fix = fix; c.fixData = data; c.safeFix = safe; c.action = action;
            out.push_back (c);
        };
        const auto secPerBar = 4.0 * 60.0 / juce::jlimit (60.0, 200.0, in.bpm);
        const auto& S = in.sections;
        int heardBars = 0;
        for (const auto& b : in.bars) heardBars += b.heard ? 1 : 0;
        if (in.bars.empty()) for (const auto& s : in.sections) heardBars += s.heardBars;   // scored from its sections only (a reference)
        const auto songHeard = heardBars >= 16;
        std::vector<int> drops, breaks, builds;
        for (const auto& s : S) { if (s.type == T::Drop) drops.push_back (s.index); if (s.type == T::Breakdown) breaks.push_back (s.index); if (s.type == T::Build) builds.push_back (s.index); }
        auto sec = [&] (int i) -> const SectionStat* { return i >= 0 && i < (int) S.size() ? &S[(size_t) i] : nullptr; };
        auto heardSec = [&] (int i) { const auto* s = sec (i); return s != nullptr && s->heard(); };
        const auto totalSeconds = in.map.totalBars * secPerBar;

        // ===== Against the label =====
        {
            const auto minutes = (float) (totalSeconds / 60.0);
            add (aReference, "Length", in.map.count > 0, fromOutside (P.minutes.outside (minutes)), juce::String (minutes, 1) + " min (the label: " + P.minutes.text() + ")",
                 "lengthen or shorten in Ouroboros (Arrange, or the structure page)");
            float introBars = in.map.count > 0 && in.map.sections[0].type == T::Intro ? (float) (in.map.endBar (0) - 1) : 0.0f;
            add (aReference, "Intro length", in.map.count > 0, fromOutside (P.introBars.outside (introBars)), juce::String ((int) introBars) + " bars (the label: " + P.introBars.text (0) + ")",
                 "the intro's length in Ouroboros", 1);
            float bdBars = 0.0f;
            for (auto i : breaks) bdBars = juce::jmax (bdBars, (float) S[(size_t) i].bars);
            add (aReference, "Breakdown length", ! breaks.empty(), fromOutside (P.breakdownBars.outside (bdBars)), juce::String ((int) bdBars) + " bars (the label: " + P.breakdownBars.text (0) + ")",
                 "the breakdown's length in Ouroboros");
            add (aReference, "Drops", in.map.count > 0, fromOutside (P.drops.outside ((float) drops.size())), juce::String ((int) drops.size()) + " (the label: " + P.drops.text (0) + ")");
            const auto [ref, dist] = in.profile != nullptr ? in.profile->closest (S, in.bpm) : std::pair<int, float> { -1, 0.0f };
            rep.closestRef = ref; rep.closestDistance = dist;
            if (ref >= 0)
                add (aReference, "Closest released track", songHeard, ramp (dist, 1.5f, 5.0f), "\"" + in.profile->refs[(size_t) ref].name + "\", " + juce::String (dist, 1) + " dB apart in tone",
                     "the sections furthest from it (below) say where");
            // the section matcher: each kind of section against the label's
            float worst = 0.0f; int worstIdx = -1;
            for (const auto& s : S)
            {
                if (! s.heard()) continue;
                float out2 = 0.0f; int n = 0;
                const auto sh = shape (s.band);
                for (int k = 1; k < numBands - 1; ++k) { out2 += P.tone[(size_t) s.type][(size_t) k].outside (sh[(size_t) k]); ++n; }
                out2 /= (float) juce::jmax (1, n);
                if (out2 > worst) { worst = out2; worstIdx = s.index; }
            }
            add (aReference, "Sections against the label's", songHeard && worstIdx >= 0, fromOutside (worst),
                 worstIdx >= 0 ? juce::String ("furthest: the ") + juce::String (suite::sectionName (S[(size_t) worstIdx].type)).toLowerCase() + " at bar " + juce::String (S[(size_t) worstIdx].startBar)
                               : juce::String ("every section in range"),
                 "its tone (Quintessence's targets follow the reference)", worstIdx >= 0 ? S[(size_t) worstIdx].startBar : -1);
        }

        // ===== A&R =====
        {
            // the first idea
            float songMedian = -100.0f;
            {
                std::vector<float> v; for (const auto& b : in.bars) if (b.heard) v.push_back (b.db);
                if (! v.empty()) { std::sort (v.begin(), v.end()); songMedian = v[v.size() / 2]; }
            }
            int firstIdea = -1;
            for (size_t b = 0; b < in.bars.size(); ++b) if (in.bars[b].heard && in.bars[b].onsets > 0.5f && in.bars[b].db > songMedian - 15.0f) { firstIdea = (int) b; break; }
            const auto ideaSec = firstIdea >= 0 ? (float) (firstIdea * secPerBar) : 999.0f;
            add (aAR, "The first idea", songHeard, ramp (ideaSec, 20.0f, 60.0f), firstIdea >= 0 ? "at " + juce::String (ideaSec, 0) + " s (bar " + juce::String (firstIdea + 1) + ")" : juce::String ("not found"),
                 "something to hook the ear in the first 20-30 s: a motif, a vocal hint, a percussion idea", firstIdea + 1);
            const auto firstDropSec = ! drops.empty() ? (float) ((S[(size_t) drops[0]].startBar - 1) * secPerBar) : 999.0f;
            add (aAR, "The first big moment", ! drops.empty(), ramp (firstDropSec, 150.0f, 240.0f), "the first drop at " + juce::String ((int) firstDropSec / 60) + ":" + juce::String ((int) firstDropSec % 60).paddedLeft ('0', 2),
                 "shorten the intro or the first breakdown (an A&R often skips ahead)", ! drops.empty() ? S[(size_t) drops[0]].startBar : -1);
            // drop impact
            float worstImpact = 99.0f; int worstDrop = -1;
            for (auto d : drops)
                if (d > 0 && heardSec (d) && heardSec (d - 1))
                {
                    const auto imp = S[(size_t) d].firstDb - S[(size_t) d - 1].lastDb;
                    if (imp < worstImpact) { worstImpact = imp; worstDrop = d; }
                }
            add (aAR, "Drop impact", worstDrop >= 0, fromOutside (P.impactDb.outside (worstImpact)) * (worstImpact < 1.0f ? 0.5f : 1.0f),
                 worstDrop >= 0 ? db (worstImpact) + " from the build into the drop at bar " + juce::String (S[(size_t) worstDrop].startBar) + " (the label: " + P.impactDb.text() + ")" : juce::String(),
                 "hold the build back, a bigger lead-in (the director: \"make the drop hit harder\")", worstDrop >= 0 ? S[(size_t) worstDrop].startBar : -1, true,
                 suite::Fix::BuildImpact, 0, false, "director: make the drops hit harder");
            // later drops outdo the first
            if (drops.size() >= 2 && heardSec (drops.front()) && heardSec (drops.back()))
            {
                const auto& a = S[(size_t) drops.front()]; const auto& b = S[(size_t) drops.back()];
                const auto lift = b.db - a.db, more = b.onsetsPerBar - a.onsetsPerBar;
                const auto score = juce::jmax (fromOutside (P.finalLiftDb.outside (lift)), more > 1.0f ? 0.8f : 0.0f);
                add (aAR, "The last drop outdoes the first", true, score, db (lift) + " louder, " + juce::String (more, 1) + " more events a bar",
                     "more in the last drop: a layer, the key change, the motif recalled bigger (Bigger later drops)", b.startBar, false, suite::Fix::LaterDropBigger, 0, false);
            }
            // contrast between neighbours
            int flat = 0, flatBar = -1;
            for (size_t i = 1; i < S.size(); ++i)
                if (S[i].heard() && S[i - 1].heard() && std::abs (S[i].db - S[i - 1].db) < 1.0f && distance (S[i].band, S[i - 1].band) < 1.0f
                    && std::abs (S[i].onsetsPerBar - S[i - 1].onsetsPerBar) < 1.0f)
                { ++flat; if (flatBar < 0) flatBar = S[i].startBar; }
            add (aAR, "Contrast between sections", S.size() >= 2 && songHeard, 1.0f - 0.3f * (float) flat,
                 flat == 0 ? juce::String ("every section change is heard") : juce::String (flat) + " change(s) you can hardly hear (first at bar " + juce::String (flatBar) + ")",
                 "a different element, a filter, a drop-out or a mood change at the boundary", flatBar);
            // repetition: 16+ static bars with nothing planned
            int longest = 0, longestAt = -1;
            {
                int run = 0, runStart = 0;
                for (size_t b = 1; b < in.bars.size(); ++b)
                {
                    const auto& x = in.bars[b]; const auto& y = in.bars[b - 1];
                    const auto same = x.heard && y.heard && std::abs (x.db - y.db) < 0.6f && distance (x.band, y.band) < 0.6f && std::abs (x.onsets - y.onsets) < 1.5f;
                    bool planned = false;
                    if (in.plan != nullptr)
                        for (const auto& m : in.plan->moments) if (m.hero != suite::director::pNone && (int) std::floor (m.atQ / in.barQ) == (int) b) planned = true;
                    if (same && ! planned) { if (run == 0) runStart = (int) b; ++run; }
                    else run = 0;
                    if (run > longest) { longest = run; longestAt = runStart; }
                }
            }
            add (aAR, "Nothing stands still", songHeard, ramp ((float) longest, 12.0f, 32.0f), longest >= 16 ? juce::String (longest) + " bars without change from bar " + juce::String (longestAt + 1)
                                                                                                           : juce::String ("something moves at least every 16 bars"),
                 "a fill, a layer or a director move (pin one there)", longestAt + 1, false, suite::Fix::None, 0, false, longest >= 16 ? "director: pin" : juce::String());
            // ear candy / pacing
            float songOnsets = 0.0f; int heard = 0;
            for (const auto& b : in.bars) if (b.heard) { songOnsets += b.onsets; ++heard; }
            const auto perMin = heard > 0 ? songOnsets / (float) heard * (float) (in.bpm / 4.0) : 0.0f;
            add (aAR, "Pacing (new events)", songHeard, fromOutside (P.onsetsPerMinute.outside (perMin)), juce::String (perMin, 0) + " a minute (the label: " + P.onsetsPerMinute.text (0) + ")",
                 perMin < P.onsetsPerMinute.lo ? juce::String ("more ear candy: fills, throws, percussion (the director: \"busier\")") : juce::String ("fewer events: it's cluttered (the director: \"simpler\")"),
                 -1, false, suite::Fix::None, 0, false, perMin < P.onsetsPerMinute.lo ? "director: busier" : "director: simpler");
            if (in.plan != nullptr && totalSeconds > 0.0)
            {
                int led = 0; for (const auto& m : in.plan->moments) led += m.hero != suite::director::pNone ? 1 : 0;
                const auto pm = (float) (led / (totalSeconds / 60.0));
                add (aAR, "Planned moments", true, juce::jmin (ramp (pm, 1.2f, 0.4f), ramp (pm, 3.0f, 5.0f)), juce::String (pm, 1) + " a minute", "the director's story (Restrained ... Festival)");
            }
            // the breakdown
            float loudest = -100.0f; for (auto d : drops) if (heardSec (d)) loudest = juce::jmax (loudest, S[(size_t) d].db);
            for (auto b : breaks)
            {
                if (! heardSec (b) || loudest < -99.0f) continue;
                const auto depth = loudest - S[(size_t) b].db;
                bool hint = false;
                if (in.plan != nullptr) for (const auto& m : in.plan->moments) if (m.kind == suite::director::kBreakHint && m.section == b && m.hero != suite::director::pNone) hint = true;
                const auto from = (S[(size_t) b].startBar - 1) * in.barQ, to = from + S[(size_t) b].bars * in.barQ;
                for (const auto& n : in.song.hook) if (n.start >= from && n.start < to) hint = true;
                add (aAR, "The breakdown at bar " + juce::String (S[(size_t) b].startBar), true, fromOutside (P.breakdownDepthDb.outside (depth)) * (hint ? 1.0f : 0.8f),
                     db (-depth) + " under the drops (the label: " + P.breakdownDepthDb.text() + ")" + (hint ? ", the hook hinted" : ", no hint of the hook"),
                     depth > P.breakdownDepthDb.hi ? juce::String ("too empty: a pad or the hook's hint") : depth < P.breakdownDepthDb.lo ? juce::String ("too busy: thin it out") : juce::String ("a hint of the hook (the director: a hook hint)"),
                     S[(size_t) b].startBar, false, depth > P.breakdownDepthDb.hi ? suite::Fix::BreakdownLouder : depth < P.breakdownDepthDb.lo ? suite::Fix::BreakdownQuieter : suite::Fix::None,
                     0, false, depth < P.breakdownDepthDb.lo ? "autopilot:" + juce::String ((int) suite::Autopilot::contrastF) : juce::String());
                break;   // the first breakdown speaks for the rest
            }
            // the emotional peak
            {
                int bestBar = -1; float best = -200.0f;
                for (size_t b = 0; b + 8 <= in.bars.size(); ++b)
                {
                    std::vector<float> v; for (size_t k = b; k < b + 8; ++k) if (in.bars[k].heard) v.push_back (in.bars[k].db);
                    const auto m = powerMeanDb (v);
                    if (v.size() >= 6 && m > best) { best = m; bestBar = (int) b; }
                }
                const auto where = in.map.totalBars > 0 && bestBar >= 0 ? (float) bestBar / (float) in.map.totalBars : 0.0f;
                add (aAR, "The emotional peak", songHeard && bestBar >= 0, ramp (where, 0.55f, 0.3f), "the biggest 8 bars at bar " + juce::String (bestBar + 1) + " (" + juce::String (juce::roundToInt (where * 100.0f)) + "% in)",
                     "the final drop should be the biggest moment: Bigger later drops, the key change", bestBar + 1, false, suite::Fix::LaterDropBigger);
            }
            // tension and release
            int broken = 0, brokenBar = -1;
            for (auto b : builds)
                if (heardSec (b) && b + 1 < (int) S.size() && S[(size_t) b + 1].type == T::Drop && heardSec (b + 1))
                {
                    const auto rise = S[(size_t) b].lastDb - S[(size_t) b].firstDb, impact = S[(size_t) b + 1].firstDb - S[(size_t) b].lastDb;
                    if (rise > 0.0f && impact < 0.4f * rise) { ++broken; if (brokenBar < 0) brokenBar = S[(size_t) b + 1].startBar; }
                }
            add (aAR, "The drops deliver what the builds promise", ! builds.empty() && songHeard, 1.0f - 0.4f * (float) broken,
                 broken == 0 ? juce::String ("every build is paid off") : juce::String (broken) + " build(s) rise more than the drop lands (bar " + juce::String (brokenBar) + ")",
                 "hold the build back (the director's level ride) or a bigger drop entrance", brokenBar, false, suite::Fix::BuildImpact);
            // the signature moment
            const auto motif = in.plan != nullptr && in.plan->motif != suite::director::gNone;
            add (aAR, "One signature idea", true, motif || ! in.song.hook.empty() ? 1.0f : 0.5f,
                 motif ? juce::String ("the director's motif: ") + suite::director::gesture (in.plan->motif).name : ! in.song.hook.empty() ? juce::String ("the hook") : juce::String ("no motif or hook found"),
                 "a hook, or a motif the song keeps coming back to");
            // drop variation
            if (drops.size() >= 2 && heardSec (drops[0]) && heardSec (drops[1]))
            {
                const auto& a = S[(size_t) drops[0]]; const auto& b = S[(size_t) drops[1]];
                const auto d = distance (a.band, b.band) + 0.5f * std::abs (a.onsetsPerBar - b.onsetsPerBar) + 0.5f * std::abs (b.db - a.db);
                add (aAR, "Drop 2 isn't a copy of drop 1", true, ramp (d, 1.5f, 0.3f), "they differ by " + juce::String (d, 1) + " (tone, events, level)",
                     "a hook variation, a new layer, a kit swap for drop 2 (Correspondence's variations)", b.startBar, false, suite::Fix::LaterDropBigger);
            }
            // the outro
            if (in.map.count > 0 && in.map.sections[(size_t) in.map.count - 1].type == T::Outro && heardSec (in.map.count - 1))
            {
                const auto& o = S[(size_t) in.map.count - 1];
                add (aAR, "The outro winds down", true, ramp (o.firstDb - o.lastDb, 3.0f, 0.0f), "it falls " + db (o.lastDb - o.firstDb) + " to its end",
                     "elements leaving every 8 bars, the space closing", o.startBar);
            }
        }

        // ===== Writing =====
        {
            const auto& h = in.song.hook;
            const auto have = in.song.valid && h.size() >= 4;
            if (have)
            {
                int lo = 127, hi = 0; size_t peakAt = 0;
                for (size_t i = 0; i < h.size(); ++i) { lo = juce::jmin (lo, h[i].pitch); if (h[i].pitch > hi) { hi = h[i].pitch; peakAt = i; } }
                const auto range = hi - lo;
                add (aWriting, "The hook's range", true, juce::jmin (ramp ((float) range, 7.0f, 3.0f), ramp ((float) range, 15.0f, 20.0f)), juce::String (range) + " semitones",
                     "a hook moves 7-15 semitones (Correspondence: Write it stronger)", -1, false, suite::Fix::None, 0, false, "write stronger");
                const auto peakPos = (float) peakAt / (float) juce::jmax ((size_t) 1, h.size() - 1);
                add (aWriting, "The hook's peak", true, peakPos >= 0.4f ? 1.0f : 0.6f, "its highest note " + juce::String (juce::roundToInt (peakPos * 100.0f)) + "% of the way through",
                     "the highest note late in the phrase builds to it", -1, false, suite::Fix::None, 0, false, "write stronger");
                // rhythm identity: the bars' 16th-grid patterns, how often they repeat
                std::map<int, uint32_t> perBar;
                std::set<int> lengths;
                for (const auto& n : h) { perBar[(int) std::floor ((n.start - in.song.hookStartQ) / 4.0)] |= 1u << juce::jlimit (0, 15, (int) std::lround (std::fmod (n.start - in.song.hookStartQ, 4.0) * 4.0)); lengths.insert ((int) std::lround (n.length * 4.0)); }
                std::map<uint32_t, int> counts; for (const auto& [b, mask] : perBar) ++counts[mask];
                int most = 0; for (const auto& [m, c] : counts) most = juce::jmax (most, c);
                const auto identity = perBar.empty() ? 0.0f : (float) most / (float) perBar.size();
                add (aWriting, "The hook's rhythm", true, juce::jmin (ramp (identity, 0.5f, 0.2f), lengths.size() >= 2 ? 1.0f : 0.6f),
                     juce::String (juce::roundToInt (identity * 100.0f)) + "% of its bars share a rhythm, " + juce::String ((int) lengths.size()) + " note lengths",
                     "a rhythm the hook keeps (identity) with long and short notes", -1, false, suite::Fix::None, 0, false, "write stronger");
                const auto lastPc = ((h.back().pitch - in.song.root) % 12 + 12) % 12;
                const auto minor = ! (in.song.scale == 1 || in.song.scale == 5);
                const auto stable = lastPc == 0 || lastPc == 7 || lastPc == (minor ? 3 : 4);
                add (aWriting, "The hook resolves", true, stable ? 1.0f : 0.6f, stable ? juce::String ("it ends on a chord tone of the key") : juce::String ("it ends on a passing note"),
                     "end the phrase on the root, third or fifth (or leave it open on purpose)", -1, false, suite::Fix::None, 0, false, "write stronger");
                // repetition with variation: two halves compared
                const auto half = h.size() / 2;
                int same = 0; for (size_t i = 0; i < half && half + i < h.size(); ++i) same += h[i].pitch == h[half + i].pitch ? 1 : 0;
                const auto rep2 = half > 0 ? (float) same / (float) half : 0.0f;
                add (aWriting, "Repetition with variation", true, juce::jmin (ramp (rep2, 0.5f, 0.2f), ramp (rep2, 0.85f, 1.0f)), juce::String (juce::roundToInt (rep2 * 100.0f)) + "% of the second half repeats the first",
                     rep2 > 0.85f ? juce::String ("vary the answer phrase") : juce::String ("repeat more: a hook needs to come back"), -1, false, suite::Fix::None, 0, false, "write stronger");
                // originality against your catalogue
                const auto print = hookPrint (h);
                float closest = 0.0f;
                for (const auto& o : in.otherHooks) closest = juce::jmax (closest, printSimilarity (print, o));
                add (aWriting, "Not a copy of your last hooks", ! in.otherHooks.empty(), ramp (closest, 0.6f, 0.95f), juce::String (juce::roundToInt (closest * 100.0f)) + "% like your closest earlier hook",
                     "change the hook's intervals or rhythm", -1, false, suite::Fix::None, 0, false, "write stronger");
                // the key change suits it
                if (in.song.finalLift > 0)
                    add (aWriting, "The key change suits the hook", true, ramp ((float) (hi + in.song.finalLift), 86.0f, 93.0f), "its top note lifted to MIDI " + juce::String (hi + in.song.finalLift),
                         "a smaller lift, or the hook an octave down for the final drop");
                // vocal and hook interplay
                if (! in.vocal.empty())
                {
                    int overlap = 0;
                    for (const auto& n : h) for (const auto& v : in.vocal) if (n.start < v.start + v.length && n.start + n.length > v.start) { ++overlap; break; }
                    const auto share = (float) overlap / (float) h.size();
                    add (aWriting, "The vocal and the hook take turns", true, ramp (share, 0.3f, 0.7f), juce::String (juce::roundToInt (share * 100.0f)) + "% of the hook plays over the vocal",
                         "let the hook answer the vocal (Correspondence: lead answers vocal)");
                }
            }
            else add (aWriting, "The hook", false, 1.0f, "Correspondence hasn't shared the song's parts yet (open it once)");
            // chords
            const auto& roots = in.song.chordRoots;
            auto rootsOf = [&] (T t) { std::vector<int> r; for (int c = 0; c < in.song.chordCount[(size_t) t]; ++c) r.push_back (roots[(size_t) t][(size_t) c]); return r; };
            if (in.song.valid && in.song.chordCount[(size_t) T::Drop] > 0)
            {
                const auto d = rootsOf (T::Drop);
                static const std::vector<std::vector<int>> common { { 0, 8, 3, 10 }, { 0, 5, 7, 0 }, { 8, 10, 0, 0 }, { 0, 10, 8, 10 }, { 9, 5, 0, 7 } };
                bool isCommon = false; for (const auto& c : common) if (d.size() >= 4 && std::equal (c.begin(), c.end(), d.begin())) isCommon = true;
                add (aWriting, "A fresh progression", true, isCommon ? 0.7f : 1.0f, isCommon ? juce::String ("a very common trance progression") : juce::String ("not one of the most common"),
                     "a borrowed chord, a different start or an inversion (Correspondence's harmony explorer)");
                const auto b = rootsOf (T::Breakdown);
                if (! b.empty()) add (aWriting, "The breakdown's own harmony", true, b != d ? 1.0f : 0.7f, b != d ? juce::String ("it moves somewhere new") : juce::String ("the same progression as the drop"),
                                      "start the breakdown on VI or IV (Correspondence: breakdown re-harmonisation)");
                const auto bu = rootsOf (T::Build);
                if (! bu.empty())
                {
                    const auto iv = ((d.front() - bu.back()) % 12 + 12) % 12;
                    add (aWriting, "The build points at the drop", true, iv == 5 || iv == 2 || iv == 1 || iv == 7 ? 1.0f : 0.7f,
                         iv == 5 ? juce::String ("V to I") : iv == 2 ? juce::String ("bVII to I") : "the last chord is " + juce::String (iv) + " semitones from the drop's first",
                         "end the build on V or bVII of the drop's first chord (the director's harmony moves)");
                }
            }
            // the bass fits the chords; the counter-line keeps out of the hook's way
            if (in.song.valid && ! in.song.bass.empty() && in.song.quartersPerChord > 0.0)
            {
                int strong = 0, fit = 0;
                for (const auto& n : in.song.bass)
                {
                    if (std::abs (std::fmod (n.start, 1.0)) > 0.01) continue;
                    int secI = -1; for (int i = 0; i < in.map.count; ++i) if ((in.map.sections[(size_t) i].startBar - 1) * in.barQ <= n.start + 1.0e-6) secI = i;
                    if (secI < 0) continue;
                    const auto t = in.map.sections[(size_t) secI].type;
                    const auto cnt = in.song.chordCount[(size_t) t];
                    if (cnt <= 0) continue;
                    const auto ci = (int) std::floor ((n.start - (in.map.sections[(size_t) secI].startBar - 1) * in.barQ) / in.song.quartersPerChord) % cnt;
                    const auto rel = ((n.pitch - in.song.root - roots[(size_t) t][(size_t) ci]) % 12 + 12) % 12;
                    ++strong; fit += rel == 0 || rel == 7 ? 1 : 0;
                }
                if (strong >= 8)
                    add (aWriting, "The bass fits the chords", true, ramp ((float) fit / (float) strong, 0.75f, 0.4f), juce::String (juce::roundToInt (100.0f * (float) fit / (float) strong)) + "% of the beats on the chord's root or fifth",
                         "the bass on the chord's root on the beat (Correspondence: chord-aware bass)");
                // velocities
                int flat = 0, ghosts = 0;
                for (const auto& n : in.song.bass) { flat += n.velocity >= 125 ? 1 : 0; ghosts += n.velocity < 20 ? 1 : 0; }
                const auto fs = (float) flat / (float) in.song.bass.size();
                add (aHygiene, "Velocities", true, juce::jmin (ramp (fs, 0.6f, 0.95f), ramp ((float) ghosts / (float) in.song.bass.size(), 0.15f, 0.4f)),
                     juce::String (juce::roundToInt (fs * 100.0f)) + "% at full velocity, " + juce::String (ghosts) + " near-silent notes",
                     "humanise the exports (Correspondence) and remove stray quiet notes");
            }
            if (in.song.valid && ! in.song.counter.empty() && ! h.empty())
            {
                int clash = 0;
                for (const auto& c : in.song.counter) for (const auto& n : h) if (std::abs (c.start - n.start) < 0.1f && std::abs (c.pitch - n.pitch) < 5) { ++clash; break; }
                const auto share = (float) clash / (float) in.song.counter.size();
                add (aWriting, "The counter-line keeps out of the hook's way", true, ramp (share, 0.15f, 0.5f), juce::String (juce::roundToInt (share * 100.0f)) + "% of its notes collide with the hook",
                     "move the counter-line an octave away, or into the hook's rests");
            }
        }

        // ===== Low end =====
        const auto haveMaster = in.master.valid && in.master.measuredSeconds > 10.0f;
        {
            add (aLowEnd, "Kick and bass in phase", haveMaster, ramp (in.master.lowCorrelation, 0.9f, 0.5f), "correlation below 120 Hz: " + juce::String (in.master.lowCorrelation, 2),
                 "flip the polarity where the low end cancels (Kybalion), mono lows", -1, true, suite::Fix::MonoLows, 0, true);
            if (in.kickHz > 0.0f)
            {
                const auto midi = 69.0f + 12.0f * std::log2 (in.kickHz / 440.0f);
                const auto pc = ((juce::roundToInt (midi) - in.song.root) % 12 + 12) % 12;
                const auto inKey = ! in.song.valid || pc == 0 || pc == 7 || pc == 5 || pc == (in.song.scale == 1 || in.song.scale == 5 ? 4 : 3);
                add (aLowEnd, "The kick's body", true, juce::jmin (ramp (in.kickHz, 48.0f, 75.0f) * ramp (in.kickHz, 48.0f, 35.0f) > 0.0f ? 1.0f : 0.6f, inKey ? 1.0f : 0.7f),
                     juce::String (in.kickHz, 0) + " Hz" + (inKey ? juce::String (", in key") : juce::String (", off the key")), "tune the kick to the key (the autopilot's kick tuning)");
                if (in.song.valid && ! in.song.bass.empty())
                {
                    int clash = 0, beats = 0;
                    for (const auto& n : in.song.bass)
                        if (std::abs (std::fmod (n.start, 1.0)) < 0.01)
                        {
                            ++beats;
                            const auto d = std::abs (((n.pitch - juce::roundToInt (midi)) % 12 + 12) % 12);
                            clash += d == 1 || d == 2 || d == 10 || d == 11 ? 1 : 0;
                        }
                    if (beats > 4) add (aLowEnd, "The kick's tail and the bass notes", true, ramp ((float) clash / (float) beats, 0.15f, 0.5f),
                                        juce::String (juce::roundToInt (100.0f * (float) clash / (float) beats)) + "% of bass notes on the beat rub against the kick's pitch",
                                        "shorter kick tail, or the bass dips at the kick's note (the autopilot)");
                }
            }
            // per section: the sub, the low mids, the breakdown's low end
            std::vector<float> dropSub;
            for (auto d : drops) if (heardSec (d)) dropSub.push_back (powerSumDb (S[(size_t) d].band, 0, 1) - S[(size_t) d].db);
            if (dropSub.size() >= 2)
            {
                const auto spread = *std::max_element (dropSub.begin(), dropSub.end()) - *std::min_element (dropSub.begin(), dropSub.end());
                add (aLowEnd, "The sub stays even across the drops", true, ramp (spread, 2.0f, 5.0f), "it moves " + juce::String (spread, 1) + " dB between drops", "the bass and kick levels per drop (Prima's suggestions)");
            }
            if (! drops.empty() && heardSec (drops[0]))
            {
                const auto sh = shape (S[(size_t) drops[0]].band);
                const auto lowOut = juce::jmax (P.tone[(size_t) T::Drop][0].outside (sh[0]), P.tone[(size_t) T::Drop][1].outside (sh[1]), P.tone[(size_t) T::Drop][2].outside (sh[2]));
                add (aLowEnd, "Kick and bass against the label", true, fromOutside (lowOut), "the drop's lows " + db (sh[1] - P.tone[(size_t) T::Drop][1].median) + " against the label's",
                     "the kick and bass levels and their low shelves", S[(size_t) drops[0]].startBar, false, sh[1] > P.tone[(size_t) T::Drop][1].hi ? suite::Fix::SectionLowsDown : suite::Fix::None, (uint32_t) T::Drop, true);
                const auto lm = juce::jmax (P.tone[(size_t) T::Drop][3].outside (sh[3]), P.tone[(size_t) T::Drop][4].outside (sh[4]));
                add (aLowEnd, "The low mids (125-250 Hz)", true, fromOutside (lm), db (0.5f * (sh[3] + sh[4]) - 0.5f * (P.tone[(size_t) T::Drop][3].median + P.tone[(size_t) T::Drop][4].median)) + " against the label",
                     "who owns 100-250 Hz: the bass, not the pads (Quintessence's mud cleanup)", S[(size_t) drops[0]].startBar, false, suite::Fix::ReverbLowCut, 0, true);
            }
            for (auto b : breaks)
            {
                if (! heardSec (b)) continue;
                const auto sh = shape (S[(size_t) b].band);
                const auto over = sh[0] - P.tone[(size_t) T::Breakdown][0].hi;
                add (aLowEnd, "The breakdown's low end", true, ramp (over, 0.0f, 6.0f), over > 0.0f ? "sub " + juce::String (over, 1) + " dB more than the label's breakdowns" : juce::String ("clean"),
                     "high-pass what isn't meant to rumble there (Prima: the breakdown's low cut)", S[(size_t) b].startBar, false, suite::Fix::SectionLowsDown, (uint32_t) T::Breakdown, true);
                break;
            }
            if (in.song.valid && in.song.bass.size() >= 8)
            {
                int lo = 127, hi = 0; for (const auto& n : in.song.bass) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
                add (aLowEnd, "The bass notes stay even", true, juce::jmin (ramp ((float) (hi - lo), 12.0f, 20.0f), ramp ((float) lo, 33.0f, 26.0f)),
                     juce::String (hi - lo) + " semitones, lowest MIDI " + juce::String (lo), "keep the bass within an octave or so; fold notes under D1 up (the autopilot)");
            }
            for (const auto& t : in.tracks)
                if (t.role == 2 && t.levelDb > -60.0f)
                {
                    const auto harm = powerSumDb (t.bands, 4, 5) - powerSumDb (t.bands, 1, 2);
                    add (aLowEnd, "The bass on small speakers", true, ramp (harm, -14.0f, -26.0f), "its harmonics (250 Hz-1 kHz) " + db (harm) + " against its lows",
                         "a little saturation on the bass (the autopilot's harmonics for small speakers)");
                    break;
                }
        }

        // ===== Mids, top and space =====
        {
            const RoleLevel* lead = nullptr; const RoleLevel* pads = nullptr; const RoleLevel* vocal = nullptr; float fxLevel = -100.0f;
            for (const auto& t : in.tracks)
            {
                if (t.levelDb < -60.0f) continue;
                if (t.role == 5 && (lead == nullptr || t.levelDb > lead->levelDb)) lead = &t;
                if (t.role == 4 && (pads == nullptr || t.levelDb > pads->levelDb)) pads = &t;
                if (t.role == 6 && (vocal == nullptr || t.levelDb > vocal->levelDb)) vocal = &t;
                if (t.role == 7) fxLevel = juce::jmax (fxLevel, t.levelDb);
            }
            if (lead != nullptr && pads != nullptr)
            {
                const auto edge = powerSumDb (lead->bands, 5, 7) - powerSumDb (pads->bands, 5, 7);
                add (aMids, "The lead cuts through", true, ramp (edge, 3.0f, -3.0f), "the lead " + db (edge) + " over the pads at 1-4 kHz",
                     "the pads make room at 2-3 kHz for the lead (Quintessence's masking dips)", -1, true, suite::Fix::BalanceStep, 0, true);
                const auto mud = powerSumDb (pads->bands, 3, 4) - pads->levelDb;
                add (aMids, "Pad and supersaw mud", true, ramp (mud, -3.0f, 2.0f), "the pads' 125-250 Hz " + db (mud) + " against their level",
                     "a low cut and a dip at 200-300 Hz on the pads (Quintessence)", -1, false, suite::Fix::ReverbLowCut, 0, true);
            }
            if (lead != nullptr && vocal != nullptr)
            {
                const auto d = std::abs (powerSumDb (lead->bands, 5, 7) - powerSumDb (vocal->bands, 5, 7));
                add (aMids, "One voice at a time (lead / vocal)", true, ramp (d, 4.0f, 1.0f), "the lead and the vocal within " + juce::String (d, 1) + " dB at 1-4 kHz",
                     "the lead steps back while the vocal sings (the autopilot's turn-taking)");
            }
            if (fxLevel > -99.0f && lead != nullptr)
                add (aMids, "FX level discipline", true, ramp (fxLevel - lead->levelDb, 0.0f, 6.0f), "the loudest FX " + db (fxLevel - lead->levelDb) + " against the lead",
                     "risers and impacts below the main parts (Azoth's levels)");
            if (! drops.empty() && heardSec (drops[0]))
            {
                const auto sh = shape (S[(size_t) drops[0]].band);
                const auto top = P.tone[(size_t) T::Drop][9].outside (sh[9]) + P.tone[(size_t) T::Drop][8].outside (sh[8]);
                add (aMids, "Top-end polish", true, fromOutside (0.5f * top), "air (8-16 kHz) " + db (sh[9] - P.tone[(size_t) T::Drop][9].median) + " against the label",
                     sh[9] < P.tone[(size_t) T::Drop][9].median ? juce::String ("more air: brighter (the master's tilt)") : juce::String ("less top: darker"), S[(size_t) drops[0]].startBar, false,
                     sh[9] < P.tone[(size_t) T::Drop][9].median ? suite::Fix::Brighter : suite::Fix::Darker, 0, true);
                const auto harsh = sh[7] - P.tone[(size_t) T::Drop][7].hi;
                add (aMids, "Air without harshness", true, ramp (harsh, 0.0f, 4.0f), harsh > 0.0f ? "2-5 kHz " + juce::String (harsh, 1) + " dB over the label's" : juce::String ("no harsh build-up"),
                     "tame 2-5 kHz (the autopilot's harshness dips), keep the air", S[(size_t) drops[0]].startBar, false, suite::Fix::Darker, 0, true);
                add (aMids, "Headphone fatigue", true, ramp (sh[6] + sh[7] - P.tone[(size_t) T::Drop][6].hi - P.tone[(size_t) T::Drop][7].hi, 0.0f, 6.0f),
                     "sustained 1-4 kHz in the drops", "the de-esser and the spike tamer on the bright parts");
            }
            float worstW = 0.0f; int worstWBar = -1; juce::String worstWText;
            for (const auto& s : S)
            {
                if (! s.heard()) continue;
                const auto o = P.width[(size_t) s.type].outside (s.width());
                if (o > worstW) { worstW = o; worstWBar = s.startBar; worstWText = juce::String (suite::sectionName (s.type)).toLowerCase() + " side " + db (s.width()) + " (the label: " + P.width[(size_t) s.type].text() + ")"; }
            }
            add (aMids, "Width per section", songHeard, fromOutside (worstW), worstWBar >= 0 ? worstWText : juce::String ("every section's width in range"),
                 "Quintessence's width per track, the director's narrow-then-wide", worstWBar);
        }

        // ===== Hygiene =====
        {
            int clipping = 0, missing = 0, idle = 0, squashed = 0;
            juce::StringArray clipNames, idleNames;
            for (const auto& e : in.entries)
            {
                if (e.has (suite::Status::clipping)) { ++clipping; clipNames.add (e.track); }
                if (e.has (suite::Status::missingFiles)) ++missing;
                if (e.has (suite::Status::idle)) { ++idle; idleNames.add (e.plugin + " on " + e.track); }
                if (e.has (suite::Status::squashedInput)) ++squashed;
            }
            add (aHygiene, "No clipping inside tracks", true, clipping == 0 ? 1.0f : 0.2f, clipping == 0 ? juce::String ("none") : clipNames.joinIntoString (", "),
                 "gain staging", -1, true, suite::Fix::GainStage, 0, true);
            add (aHygiene, "No missing samples", true, missing == 0 ? 1.0f : 0.0f, missing == 0 ? juce::String ("all found") : juce::String (missing) + " plugin(s) with missing files", "relink the samples", -1, true,
                 suite::Fix::SessionHealth);
            add (aHygiene, "Nothing forgotten", true, idle == 0 ? 1.0f : 0.7f, idle == 0 ? juce::String ("every plugin is doing something") : idleNames.joinIntoString (", "), "remove plugins that do nothing (Project tidy)");
            add (aHygiene, "Nothing squashed before the mix", true, squashed == 0 ? 1.0f : 0.6f, squashed == 0 ? juce::String ("none") : juce::String (squashed) + " track(s) arrive squashed",
                 "back off the limiters or heavy compression on those tracks", -1, false, suite::Fix::LessSquash, 0, false);
            // duplicate layers
            int dup = 0; juce::String dupName;
            for (size_t i = 0; i < in.tracks.size(); ++i)
                for (size_t k = i + 1; k < in.tracks.size(); ++k)
                {
                    const auto& a = in.tracks[i]; const auto& b = in.tracks[k];
                    if (a.role != b.role || a.levelDb < -40.0f || b.levelDb < -40.0f) continue;
                    float worst = 0.0f; for (int x = 0; x < numBands; ++x) worst = juce::jmax (worst, std::abs (a.bands[(size_t) x] - b.bands[(size_t) x]));
                    if (worst < 1.0f) { ++dup; dupName = a.track + " / " + b.track; }
                }
            add (aHygiene, "No accidental duplicates", true, dup == 0 ? 1.0f : 0.5f, dup == 0 ? juce::String ("no two tracks play the same thing") : "the same sound on " + dupName, "mute or remove the copy");
            // timing
            float worstT = 0.0f; int worstRole = 0;
            for (const auto& [role, ms] : in.timingMs) if (ms > worstT) { worstT = ms; worstRole = role; }
            add (aHygiene, "Timing tightness", ! in.timingMs.empty(), ramp (worstT, 8.0f, 25.0f), worstT > 0.0f ? juce::String (suite::director::roleName (worstRole)) + " hits sit " + juce::String (worstT, 0) + " ms off the grid" : juce::String ("tight"),
                 "quantise or line up that part (the autopilot's layer alignment)");
            // level jumps at changes nobody planned
            int jumps = 0, jumpBar = -1;
            for (size_t i = 1; i < S.size(); ++i)
            {
                if (! S[i].heard() || ! S[i - 1].heard() || S[i].type == T::Drop || S[i].type == T::Breakdown) continue;
                if (std::abs (S[i].firstDb - S[i - 1].lastDb) > 6.0f) { ++jumps; if (jumpBar < 0) jumpBar = S[i].startBar; }
            }
            add (aHygiene, "No unplanned level jumps", songHeard, 1.0f - 0.3f * (float) jumps, jumps == 0 ? juce::String ("none") : juce::String (jumps) + " jump(s) of over 6 dB (first at bar " + juce::String (jumpBar) + ")",
                 "the director evens them out once Mentalism has measured them; or the faders", jumpBar);
            if (in.fxRepitched > 0) add (aHygiene, "FX in key", true, 0.8f, juce::String (in.fxRepitched) + " out-of-key FX notes were re-pitched by the autopilot", "check those FX samples are in the song's key");
            if (in.clicksGuarded > 0) add (aHygiene, "Clean edits", true, 0.85f, juce::String (in.clicksGuarded) + " clicks at clip edges were smoothed", "fade the clip edges on those samples");
            if (in.master.valid && in.master.readiness > 0)
                add (aHygiene, "Premaster readiness (Mentalism)", true, ramp ((float) in.master.readiness, 85.0f, 50.0f), juce::String (in.master.readiness) + "/100", "Mentalism's Premaster report says what holds it back", -1, true);
        }

        // ===== Translation and mono =====
        {
            add (aTranslation, "Mono", haveMaster, ramp (in.master.monoLossDb, 3.0f, 8.0f) * (in.master.correlation < 0.0f ? 0.4f : 1.0f), "the mono sum " + juce::String (in.master.monoLossDb, 1) + " dB down",
                 "mono lows, narrower wide parts", -1, true, suite::Fix::MonoLows, 0, true);
            if (! drops.empty() && heardSec (drops[0]))
            {
                const auto sh = shape (S[(size_t) drops[0]].band);
                const auto& tn = P.tone[(size_t) T::Drop];
                // a phone: no sub: the bass and kick must be heard through 250 Hz-1 kHz
                add (aTranslation, "On a phone", true, fromOutside (tn[4].outside (sh[4]) + (sh[4] < tn[4].lo ? 0.5f : 0.0f)), "250-500 Hz (what a phone plays of the bass) " + db (sh[4] - tn[4].median),
                     "harmonics on the bass, the kick's click (the autopilot)");
                add (aTranslation, "On earbuds", true, ramp (sh[1] - sh[3], 8.0f, 14.0f), "the sub " + db (sh[1] - sh[3]) + " over the low mids", "less sub, more of the bass's body");
                add (aTranslation, "In a car", true, ramp (sh[2] + sh[3] - tn[2].hi - tn[3].hi, 0.0f, 6.0f), "60-250 Hz against the label", "a little less low-mid on the bass and pads",
                     -1, false, suite::Fix::ReverbLowCut, 0, true);
                add (aTranslation, "On a club system", true, ramp (tn[0].lo - sh[0], 0.0f, 6.0f), "the sub (30-60 Hz) " + db (sh[0] - tn[0].median) + " against the label", "the kick's and bass's sub");
                add (aTranslation, "At a low volume", true, ramp (tn[6].lo - sh[6], 0.0f, 5.0f), "1-2 kHz (the hook and the kick's click) " + db (sh[6] - tn[6].median),
                     "the lead and the kick's click a little more forward");
            }
            int wide = 0, wideBar = -1;
            for (const auto& s : S) if (s.heard() && s.width() > -3.0f) { ++wide; if (wideBar < 0) wideBar = s.startBar; }
            add (aTranslation, "Mono per section", songHeard, 1.0f - 0.3f * (float) wide, wide == 0 ? juce::String ("every section survives in mono") : juce::String (wide) + " section(s) very wide (first at bar " + juce::String (wideBar) + ")",
                 "the breakdown's pads and reverbs narrower (Quintessence's width)", wideBar);
        }

        // ===== Balance =====
        {
            std::map<int, float> level;
            for (const auto& t : in.tracks)
                if (t.levelDb > -70.0f) level[t.role] = level.count (t.role) ? (float) (10.0 * std::log10 (std::pow (10.0, level[t.role] / 10.0) + std::pow (10.0, t.levelDb / 10.0))) : t.levelDb;
            struct Pair { int a, b; float lo, hi; const char* name; };
            static const Pair pairs[] = { { 1, 2, -3.0f, 4.0f, "Kick against bass" }, { 5, 4, -1.0f, 6.0f, "Lead against pads" }, { 6, 4, 0.0f, 8.0f, "Vocal against the pads" }, { 7, 5, -12.0f, 0.0f, "FX against the lead" } };
            float worst = 0.0f; juce::String worstName;
            for (const auto& p : pairs)
            {
                if (! level.count (p.a) || ! level.count (p.b)) continue;
                const auto d = level[p.a] - level[p.b];
                const auto o = d < p.lo ? (p.lo - d) / 3.0f : d > p.hi ? (d - p.hi) / 3.0f : 0.0f;
                add (aBalance, p.name, true, fromOutside (o), db (d) + " (a good range: " + juce::String (p.lo, 0) + " to " + juce::String (p.hi, 0) + ")",
                     "Mentalism's balance loop moves each track a step at a time", -1, false, suite::Fix::BalanceStep, 0, true);
                if (o > worst) { worst = o; worstName = p.name; }
            }
            if (level.empty()) add (aBalance, "Role balance", false, 1.0f, "put Quintessence on the tracks to measure the balance");
            // the balance kept from the first to the last drop
            if (drops.size() >= 2)
            {
                float drift = 0.0f; int roleDrift = 0;
                for (const auto& [role, per] : in.roleLevelPerType)
                    juce::ignoreUnused (per);
                // per kind of section only (the drops share one): measured per type, so compare drop with breakdown balance instead
                for (const auto& [role, per] : in.roleLevelPerType)
                {
                    const auto d = per[(size_t) T::Drop], b = per[(size_t) T::Breakdown];
                    if (d < -90.0f || b < -90.0f || role == 1) continue;
                    if (std::abs (d - b) > drift) { drift = std::abs (d - b); roleDrift = role; }
                }
                if (! in.roleLevelPerType.empty())
                    add (aBalance, "Balance per section", true, ramp (drift, 8.0f, 14.0f), juce::String (suite::director::roleName (roleDrift)) + " moves " + juce::String (drift, 1) + " dB between drop and breakdown",
                         "Prima Materia's section levels for that track");
            }
        }

        extraChecks (in, P, out);
        checks3 (in, P, out);
        checks4 (in, P, out);
        checks5 (in, P, out);
        // a few earlier checks name the switch that now fixes them
        for (auto& c : out)
        {
            namespace AP = suite::Autopilot;
            if (c.action.isNotEmpty()) continue;
            if (c.name == "The last drop outdoes the first") c.action = "autopilot:" + juce::String ((int) AP::finalPeakF);
            else if (c.name == "Kick and bass paired") c.action = "autopilot:" + juce::String ((int) AP::kickBassPairF);
        }

        // one section only: the checks that point inside it (the rest don't count)
        if (in.focusSection >= 0 && in.focusSection < in.map.count)
        {
            const auto from = in.map.sections[(size_t) in.focusSection].startBar, to = in.map.endBar (in.focusSection);
            for (auto& c : out) if (c.bar < from || c.bar >= to) c.measured = false;
        }
        // the label's own minimums: a check under its minimum blocks the gate
        for (auto& c : out)
            if (const auto it = P.minScore.find (c.name); it != P.minScore.end() && c.measured && c.score < it->second) { c.blocking = true; c.belowMinimum = true; }

        // ===== scores =====
        float total = 0.0f, weights = 0.0f;
        for (int a = 0; a < numAreas; ++a)
        {
            float s = 0.0f; int n = 0;
            for (const auto& c : out) if (c.area == a && c.measured) { s += c.score; ++n; }
            rep.areaMeasured[(size_t) a] = n > 0;
            rep.areaScore[(size_t) a] = n > 0 ? s / (float) n : 0.0f;
            if (n > 0) { total += rep.areaScore[(size_t) a] * areaWeight (a); weights += areaWeight (a); }
        }
        rep.overall = weights > 0.0f ? juce::roundToInt (100.0f * total / weights) : 0;
        // the gate: the label's threshold, and nothing blocking
        for (const auto& c : out) if (c.measured && c.blocking && (c.score < 0.5f || c.belowMinimum)) rep.blockers.add (c.name + ": " + c.detail + (c.belowMinimum ? " (under this label's minimum)" : ""));
        rep.ready = rep.overall >= P.readyThreshold && rep.blockers.isEmpty() && weights > 0.0f && songHeard;   // never "ready" unheard
        rep.coverage = in.map.totalBars > 0 ? juce::jlimit (0.0f, 1.0f, (float) heardBars / (float) in.map.totalBars) : 0.0f;
        // the next action: the biggest weighted gap
        const Check* next = nullptr; float gap = 0.0f;
        for (const auto& c : out)
        {
            if (! c.measured) continue;
            const auto g = (1.0f - c.score) * areaWeight (c.area) * (c.blocking ? 2.0f : 1.0f);
            if (g > gap + 1.0e-6f) { gap = g; next = &c; }
        }
        rep.nextAction = next != nullptr ? next->name + ": " + next->explain : juce::String ("nothing: send it");
        // the verdict, as an A&R might say it
        const Check* best = nullptr;
        for (const auto& c : out) if (c.measured && c.area == aAR && (best == nullptr || c.score > best->score)) best = &c;
        const Check* worstAR = nullptr;
        for (const auto& c : out) if (c.measured && (c.area == aAR || c.area == aWriting) && (worstAR == nullptr || c.score < worstAR->score)) worstAR = &c;
        if (best != nullptr && worstAR != nullptr && worstAR->score < 0.8f)
            rep.verdict = juce::String (best->score >= 0.9f ? "Strong " : "Decent ") + best->name.toLowerCase() + ", but " + worstAR->name.toLowerCase() + " (" + worstAR->detail + ").";
        else if (rep.overall >= P.readyThreshold) rep.verdict = "This could go to the label: nothing stands out as weak.";
        else rep.verdict = rep.overall > 0 ? "Getting there: " + rep.nextAction : "Play the song through once so the suite can hear it.";
        return rep;
    }
}

#include "LabelChecksMore.h"
#include "LabelChecks3.h"
#include "LabelChecks4.h"
#include "LabelChecks5.h"
