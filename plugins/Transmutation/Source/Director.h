#pragma once

// The director: decides which moves this track makes, and where, from the song map, the track's
// role and what it hears, the genre, the spice, your Auto / Always / Never choices, your locks
// and re-rolls, what you've kept in earlier songs, and what the other tracks are doing.
// Pure and deterministic (the same inputs give the same plan), so every track agrees on the
// song-wide choices and a plan only changes when something it depends on changes.

#include "Effects.h"
#include "Recipes.h"
#include "SectionBus.h"
#include <algorithm>
#include <map>
#include <vector>

namespace trm
{
    struct Profile
    {
        bool known = false, pitched = false, sustained = false, plucky = false, busy = false;
        bool operator== (const Profile& o) const { return known == o.known && pitched == o.pitched && sustained == o.sustained && plucky == o.plucky && busy == o.busy; }
    };

    struct OtherTrack { uint32_t id = 0; int role = 0; std::vector<Move> moves; };

    struct DirectorInput
    {
        suite::SectionMap map;
        bool haveMap = false;
        double barQ = 4.0, bpm = 138.0;
        int role = 0;
        Profile hear;
        bool pluckName = false, arpName = false;
        float spice = 1.0f, songSpice = 1.0f, builds = 1.0f, drops = 1.0f;
        int genre = gAuto;
        uint32_t songSeed = 0, trackSeed = 0;
        std::array<uint8_t, numEffects + 1> modes {};   // by move number
        std::map<uint32_t, int> locks, rerolls;          // moment -> move number / re-roll count
        std::array<float, numEffects + 1> signature {};  // how often you've kept each move
        std::vector<bool> vocalBars;                     // bars the vocal sings in (index 0 = bar 1)
        std::vector<OtherTrack> others;                  // tracks that decide before this one
        std::vector<std::pair<double, double>> hookRests;  // where the song's hook rests (Correspondence), quarters
        std::vector<std::pair<double, double>> vocalNotes; // what the vocal sang (Solve et Coagula), quarters
        // The suite director (Ouroboros): when its plan is live, every transition moment is its
        // call. This track leads (part 1, in the family it asks for), supports (2: one small
        // move) or rests (no cue). The director's silences have no moves in them.
        struct SuiteCue { double fromQ = 0.0, toQ = 0.0; int part = 0, family = -1, heroRole = 0; };
        bool suiteActive = false;
        std::vector<SuiteCue> suiteCues;
        std::vector<std::pair<double, double>> suiteSilences;
    };

    struct Plan
    {
        std::vector<Move> moves;
        std::array<int, 3> motifFamilies { -1, -1, -1 };
        int fakeDrop = -1;   // which drop (0 = first) gets the fake drop, or -1
        int genre = gUplifting;
    };

    /** The default Auto / Always / Never for every move (opt-in moves start at Never). */
    inline std::array<uint8_t, numEffects + 1> defaultModes()
    {
        std::array<uint8_t, numEffects + 1> m {};
        for (const auto& e : effects()) m[(size_t) e.id] = e.optIn ? mNever : mAuto;
        return m;
    }

    namespace dir
    {
        inline uint32_t mix (uint32_t a, uint32_t b) noexcept
        {
            auto h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
            h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
            return h;
        }
        inline float unit (uint32_t h) noexcept { return (float) (mix (h, 0x51ed27u) & 0xffffffu) / 16777216.0f; }

        struct SlotInst
        {
            uint32_t kind = 0;
            double startQ = 0.0, endQ = 0.0, sectStartQ = 0.0, sectEndQ = 0.0;
            int section = -1, dropOrdinal = -1;   // the drop this moment leads into or belongs to
            suite::SectionType type = suite::SectionType::Groove, next = suite::SectionType::Groove;
            uint32_t key = 0;
        };

        inline std::vector<SlotInst> slotsOf (const DirectorInput& in)
        {
            using T = suite::SectionType;
            std::vector<SlotInst> out;
            const auto bar = in.barQ;
            auto add = [&] (uint32_t kind, double s, double e, const SlotInst& base)
            {
                if (e <= s + 1.0e-6) return;
                auto x = base;
                x.kind = kind; x.startQ = s; x.endQ = e;
                x.key = mix (mix (kind, (uint32_t) (base.section + 7)), (uint32_t) std::lround (s * 4.0));
                out.push_back (x);
            };
            if (! in.haveMap || in.map.count == 0)
            {
                // No map: an endless groove, a phrase every 8 bars
                SlotInst base; base.section = 0; base.sectStartQ = 0.0; base.sectEndQ = 512.0 * bar;
                for (int p = 1; p <= 64; ++p) add (sPhraseEnd, (p * 8 - 1) * bar, p * 8 * bar, base);
                add (sSong, 0.0, 512.0 * bar, base);
                return out;
            }
            const auto& m = in.map;
            // drop ordinals
            std::vector<int> dropOrd ((size_t) m.count, -1);
            int drops = 0;
            for (int i = 0; i < m.count; ++i) if (m.sections[(size_t) i].type == T::Drop) dropOrd[(size_t) i] = drops++;
            const auto lastDrop = m.lastDropIndex();
            for (int i = 0; i < m.count; ++i)
            {
                SlotInst base;
                base.section = i;
                base.type = m.sections[(size_t) i].type;
                base.next = i + 1 < m.count ? m.sections[(size_t) i + 1].type : T::Outro;
                const auto hasNext = i + 1 < m.count;
                const auto S = (m.sections[(size_t) i].startBar - 1) * bar, E = (m.endBar (i) - 1) * bar;
                base.sectStartQ = S; base.sectEndQ = E;
                // the drop this section leads into (builds, breakdowns) or is
                base.dropOrdinal = dropOrd[(size_t) i];
                if (base.dropOrdinal < 0)
                    for (int k = i + 1; k < m.count; ++k) if (dropOrd[(size_t) k] >= 0) { base.dropOrdinal = dropOrd[(size_t) k]; break; }
                const auto lenBars = (E - S) / bar;
                switch (base.type)
                {
                    case T::Build:
                        add (sBuildBody, S, E, base);
                        add (sBuildEnd, E - bar, E, base);
                        if (i > 0 && m.sections[(size_t) i - 1].type == T::Breakdown) add (sBuildStart, S, S + juce::jmin (2.0 * bar, E - S), base);
                        break;
                    case T::Drop:
                        add (sDropBody, S, E, base);
                        add (sDropStart, S, S + bar, base);
                        if (i == lastDrop) { add (sFinalDrop, S, E, base); add (sFinalEnd, juce::jmax (S, E - 8.0 * bar), E, base); }
                        break;
                    case T::Breakdown:
                        add (sBreakBody, S, E, base);
                        add (sBreakStart, S, S + juce::jmin (4.0 * bar, E - S), base);
                        if (hasNext && lenBars >= 4.0 && (base.next == T::Build || base.next == T::Drop)) add (sBreakPeak, E - 2.0 * bar, E, base);
                        break;
                    case T::Outro: add (sOutro, S, E, base); break;
                    case T::Intro: case T::Groove: case T::count: default: add (sGrooveBody, S, E, base); break;
                }
                if (hasNext && base.next == T::Breakdown) add (sIntoBreak, E - 1.0, E, base);
                if (hasNext && base.type != T::Build) add (sSectionEnd, E - bar, E, base);
                if (hasNext && base.next == T::Drop && m.keyShiftOf (i + 1) > m.keyShiftOf (i)) add (sKeyChange, E - 1.0, E, base);
                for (auto P = S + 8.0 * bar; P < E - bar - 1.0e-6; P += 8.0 * bar)
                    add (sPhraseEnd, P - bar, P, base);
            }
            SlotInst song; song.section = 0; song.sectStartQ = 0.0; song.sectEndQ = m.totalBars * bar;
            add (sSong, 0.0, m.totalBars * bar, song);
            std::stable_sort (out.begin(), out.end(), [] (const SlotInst& a, const SlotInst& b) { return a.startQ < b.startQ; });
            return out;
        }

        inline bool needsMet (const Effect& e, const DirectorInput& in)
        {
            const auto r = in.role;
            const auto pitched = in.hear.known ? in.hear.pitched : (r == 2 || r == 4 || r == 5 || r == 6 || r == 0);
            const auto sustained = in.hear.known ? in.hear.sustained : (r == 4 || r == 6);
            const auto plucky = in.pluckName || in.arpName || (in.hear.known && in.hear.plucky);
            const auto busy = in.arpName || r == 3 || (in.hear.known && in.hear.busy);
            if ((e.needs & nPitched) && (! pitched || r == 1 || r == 3)) return false;
            if ((e.needs & nSustained) && ! sustained) return false;
            if ((e.needs & nPlucky) && ! plucky) return false;
            if ((e.needs & nBusy) && ! busy) return false;
            return true;
        }

        /** The window a move takes inside (or around) its moment. */
        inline std::pair<double, double> windowFor (int id, const SlotInst& s, int role, double bar)
        {
            const auto S = s.startQ, E = s.endQ;
            auto lastBeats = [&] (double beats) { return std::make_pair (juce::jmax (S, E - beats), E); };
            switch (id)
            {
                case 7:  return s.kind == sBuildEnd ? std::make_pair (S, E) : lastBeats (2.0);
                case 8: case 12: case 21: case 31: case 50: case 51: case 53: case 84: case 105: return lastBeats (1.0);
                case 46: case 109: return lastBeats (s.kind == sBreakPeak ? 1.0 : 2.0);
                case 57: return { E, E + 0.25 };
                case 101: return { E - 0.25, E };
                case 5: return { S, S + 0.25 };
                case 81: return { S + bar - 0.5, S + bar + 1.0 };
                case 32: case 67: return { E - bar, E };
                case 102: return { E - 1.0, E + bar };
                case 4:
                {
                    const auto off = role == 2 ? 0.0 : role == 4 ? 0.25 : role == 5 || role == 6 ? 0.5 : 0.375;
                    return { E - 1.0 + off, E };
                }
                case 106: return s.kind == sDropStart ? std::make_pair (S, S + 1.0) : std::make_pair (S, E);
                case 110: return s.kind == sDropStart ? std::make_pair (S, S + 2.0) : std::make_pair (S, E);
                case 111: return { S, juce::jmin (S + 2.0 * bar, s.sectEndQ) };
                case 87:
                {
                    const auto len = s.sectEndQ - s.sectStartQ;
                    if (len >= 6.0 * bar) return { S + 2.0 * bar, S + 4.0 * bar };
                    return { S, juce::jmin (E, S + 2.0 * bar) };
                }
                default: return { S, E };
            }
        }

        /** A phrase-end move placed in the hook's rest nearest the phrase's end (within a bar), and
            moved off the vocal's notes (within two beats) where it can be; the amount it keeps. */
        inline float placeAround (std::pair<double, double>& w, const SlotInst& s, const DirectorInput& in, double bar)
        {
            const auto len = w.second - w.first;
            if (s.kind == sPhraseEnd && ! in.hookRests.empty())
            {
                double best = 1.0e9;
                std::pair<double, double> chosen = w;
                for (const auto& [rs, re] : in.hookRests)
                {
                    if (re - rs < juce::jmin (len, 1.0) - 1.0e-6) continue;
                    const auto d = std::abs (re - s.endQ);
                    if (d <= bar + 1.0e-6 && d < best && re <= s.sectEndQ + 1.0e-6) { best = d; chosen = { juce::jmax (rs, re - len), re }; }
                }
                w = chosen;
            }
            if ((s.kind & (sPhraseEnd | sSectionEnd)) == 0 || in.vocalNotes.empty() || in.role == 6) return 1.0f;
            auto clash = [&] (double a, double b) { for (const auto& [vs, ve] : in.vocalNotes) if (vs < b && ve > a) return true; return false; };
            if (! clash (w.first, w.second)) return 1.0f;
            if (s.kind == sPhraseEnd)
                for (double shift : { -0.5, 0.5, -1.0, 1.0, -1.5, 1.5, -2.0, 2.0 })
                    if (! clash (w.first + shift, w.second + shift) && w.second + shift <= s.sectEndQ + 1.0e-6)
                    {
                        w = { w.first + shift, w.second + shift };
                        return 1.0f;
                    }
            return 0.6f;   // it has to stay: smaller, under the vocal
        }

        inline int priority (int role) { switch (role) { case 6: return 5; case 5: return 4; case 1: case 2: return 3; case 4: return 2; case 3: case 7: return 1; default: return 0; } }
    }

    inline Plan makePlan (const DirectorInput& in)
    {
        using namespace dir;
        Plan plan;
        const auto bar = in.barQ;
        const auto rb = roleBit (in.role);
        const auto genre = in.genre == gAuto ? genreFromTempo (in.bpm) : in.genre;
        plan.genre = genre;
        const auto mode = [&] (int id) { return in.modes[(size_t) juce::jlimit (1, numEffects, id)]; };
        const auto on = [&] (int id) { return mode (id) != mNever; };
        const auto spice = juce::jlimit (0.0f, 2.0f, in.spice);
        const auto seed = mix (in.songSeed, in.trackSeed);
        const auto slots = slotsOf (in);

        // ---- song-wide choices (the same on every track: they use only the song's seed) ----
        int drops = 0;
        if (in.haveMap) for (int i = 0; i < in.map.count; ++i) if (in.map.sections[(size_t) i].type == suite::SectionType::Drop) ++drops;
        if (drops >= 2 && mode (34) != mNever && ! in.suiteActive)   // the suite director owns the silences (and the fake drop)
        {
            const auto r = unit (mix (in.songSeed, 34u));
            if (mode (34) == mAlways || r < 0.5f * juce::jlimit (0.0f, 2.0f, in.songSpice))
                plan.fakeDrop = 1 + (int) (mix (in.songSeed, 3434u) % (uint32_t) (drops - 1));
        }
        if (on (94))
        {
            static const int fams[] = { fRepeat, fReverse, fPitch, fSpace, fGrain, fStereo, fTape, fHarmony, fFilter };
            auto h = mix (in.songSeed, 94u);
            for (size_t k = 0; k < 3; ++k)
            {
                int f;
                do { h = mix (h, (uint32_t) k + 1u); f = fams[h % 9u]; }
                while (std::find (plan.motifFamilies.begin(), plan.motifFamilies.end(), f) != plan.motifFamilies.end());
                plan.motifFamilies[k] = f;
            }
        }
        auto isMotif = [&] (const Effect& e) { return std::find (plan.motifFamilies.begin(), plan.motifFamilies.end(), (int) e.family) != plan.motifFamilies.end(); };

        auto eligible = [&] (const Effect& e, uint32_t kind)
        {
            return e.category != cDirector && (e.slots & kind) != 0 && (e.roles & rb) != 0 && mode (e.id) != mNever && needsMet (e, in);
        };
        auto vocalAt = [&] (double q)
        {
            const auto b = (int) std::floor (q / bar);
            return b >= 0 && b < (int) in.vocalBars.size() && in.vocalBars[(size_t) b];
        };
        auto othersOverlap = [&] (int id, int family, double s, double e, bool& sameFamily)
        {
            bool same = false; sameFamily = false;
            for (const auto& o : in.others)
                for (const auto& mv : o.moves)
                    if (mv.startQ < e && mv.endQ > s)
                    {
                        if (mv.effect == id) same = true;
                        if (mv.effect >= 1 && mv.effect <= numEffects && effect (mv.effect).family == family) sameFamily = true;
                    }
            return same;
        };
        auto arcOf = [&] (const SlotInst& s)
        {
            if (! on (92) || s.dropOrdinal < 0 || drops <= 1) return 1.0f;
            if (s.dropOrdinal == 0) return 0.7f;
            if (s.dropOrdinal == drops - 1) return 1.35f;
            return 1.0f;
        };

        std::map<uint32_t, int> lastByKind;   // slot kind -> effect used there last
        std::vector<int> recent;              // effects at the last few moments
        bool lastBusy = false;

        for (const auto& s : slots)
        {
            const auto body = (s.kind & bodySlots) != 0;
            const auto arc = arcOf (s);
            float p = 0.5f;
            switch (s.kind)
            {
                case sBuildEnd: p = 0.85f; break;   case sDropStart: p = 0.7f; break;   case sBreakPeak: p = 0.55f; break;
                case sBreakStart: p = 0.5f; break;  case sPhraseEnd: p = 0.3f; break;   case sSectionEnd: p = 0.45f; break;
                case sIntoBreak: p = 0.5f; break;   case sKeyChange: p = 0.9f; break;   case sBuildStart: p = 0.35f; break;
                case sFinalEnd: p = 0.6f; break;    case sBuildBody: p = 0.9f; break;   case sBreakBody: p = 0.8f; break;
                case sDropBody: p = 0.5f; break;    case sOutro: p = 0.7f; break;       case sFinalDrop: p = 0.6f; break;
                case sGrooveBody: p = 0.3f; break;  case sSong: p = 0.8f; break;        default: break;
            }
            p *= spice * arc;
            if (s.kind & (sBuildBody | sBuildEnd | sBuildStart | sKeyChange)) p *= juce::jlimit (0.0f, 2.0f, in.builds);
            if (s.kind & (sDropStart | sDropBody | sFinalDrop | sFinalEnd)) p *= juce::jlimit (0.0f, 2.0f, in.drops);
            if (! body && on (95) && lastBusy) p *= 0.45f;
            const auto quietForVocal = on (96) && in.role != 6 && ! body && vocalAt (s.startQ);
            if (quietForVocal) p *= 0.55f;

            std::vector<Move> chosen;
            auto addMove = [&] (int id, uint8_t flags, float amount)
            {
                auto w = windowFor (id, s, in.role, bar);
                if (id != 57 && id != 102 && id != 4) amount *= placeAround (w, s, in, bar);
                Move mv;
                mv.startQ = (float) w.first; mv.endQ = (float) w.second; mv.effect = id; mv.amount = amount;
                mv.seed = mix (seed, s.key ^ (uint32_t) id); mv.key = s.key; mv.slot = s.kind; mv.flags = flags;
                chosen.push_back (mv);
            };
            const auto baseAmount = juce::jlimit (0.3f, 1.4f, (0.55f + 0.35f * spice) * arc * (quietForVocal ? 0.7f : 1.0f));

            // locked moments keep their move
            if (const auto it = in.locks.find (s.key); it != in.locks.end() && it->second >= 1 && it->second <= numEffects)
            {
                const auto& e = effect (it->second);
                if ((e.slots & s.kind) && (e.roles & rb) && mode (e.id) != mNever) addMove (e.id, mfLocked, baseAmount);
            }
            // Always
            for (const auto& e : effects())
                if (mode (e.id) == mAlways && eligible (e, s.kind)
                    && std::none_of (chosen.begin(), chosen.end(), [&] (const Move& c) { return c.effect == e.id; }))
                    addMove (e.id, mfAlways, baseAmount);

            if (chosen.empty())
            {
                const auto reroll = (uint32_t) [&] { const auto it = in.rerolls.find (s.key); return it != in.rerolls.end() ? it->second : 0; }();
                auto h = mix (mix (seed, s.key), reroll * 7919u + 1u);
                const auto r1 = unit (h), r2 = unit (h ^ 0xabcdu);
                int picks = body ? (int) std::floor (p * 1.6f + r1) : (r1 < p ? 1 : 0) + (r2 < p - 1.0f ? 1 : 0);
                picks = juce::jlimit (0, body ? 3 : 2, picks);
                // the suite director decides the transitions: lead, support or rest
                int suitePart = -1, suiteFamily = -1;
                if (in.suiteActive && ! body)
                {
                    suitePart = 0;
                    for (const auto& c : in.suiteCues)
                        if (c.toQ > s.startQ - 1.0e-6 && c.fromQ < s.endQ + 1.0e-6)
                        {
                            auto part = c.part;
                            if (part == 1 && c.heroRole != 0 && c.heroRole != in.role) part = 2;   // you chose another track to lead
                            if (part == 1 || (part == 2 && suitePart != 1)) { suitePart = part; suiteFamily = c.family; }
                        }
                    picks = suitePart == 0 ? 0 : suitePart == 2 ? juce::jmin (picks, 1) : juce::jlimit (1, 2, picks);
                }
                for (int k = 0; k < picks; ++k)
                {
                    std::vector<std::pair<int, float>> cands;
                    float total = 0.0f;
                    for (const auto& e : effects())
                    {
                        if (! eligible (e, s.kind)) continue;
                        if (std::any_of (chosen.begin(), chosen.end(), [&] (const Move& c)
                                         { return c.effect == e.id || (e.group != 0 && effect (c.effect).group == e.group); }))
                            continue;
                        auto w = e.weight * (on (98) ? genreWeight (genre, e) : 1.0f);
                        if (on (94) && ! body && isMotif (e)) w *= 2.5f;
                        if (on (91)) w *= 1.0f + 0.12f * juce::jmin (8.0f, in.signature[(size_t) e.id]);
                        if (const auto it = lastByKind.find (s.kind); it != lastByKind.end() && it->second == e.id) w *= 0.25f;
                        else if (std::find (recent.begin(), recent.end(), e.id) != recent.end()) w *= 0.6f;
                        bool sameFam = false;
                        const auto win = windowFor (e.id, s, in.role, bar);
                        if (othersOverlap (e.id, e.family, win.first, win.second, sameFam)) w *= 0.3f;
                        else if (sameFam && ! body) w *= 0.7f;
                        if (quietForVocal && e.size == 3) w *= 0.3f;
                        if (on (92) && e.size == 3) w *= s.dropOrdinal == 0 ? 0.6f : (drops > 1 && s.dropOrdinal == drops - 1) ? 1.4f : 1.0f;
                        if (! body && e.size == 3 && (s.kind & (sPhraseEnd | sSectionEnd))) w *= 0.5f;
                        if (suitePart == 2 && e.size > 1) continue;
                        if (suitePart == 1 && suiteFamily >= 0) w *= e.family == suiteFamily ? 4.0f : 0.35f;
                        if (w <= 0.0f) continue;
                        cands.push_back ({ e.id, w });
                        total += w;
                    }
                    if (cands.empty() || total <= 0.0f) break;
                    auto r = unit (mix (h, (uint32_t) k + 17u)) * total;
                    int pick = cands.back().first;
                    for (const auto& [id, w] : cands) { if (r < w) { pick = id; break; } r -= w; }
                    uint8_t flags = 0;
                    if (on (94) && ! body && isMotif (effect (pick))) flags |= mfMotif;
                    if (on (91) && in.signature[(size_t) pick] >= 2.0f) flags |= mfSignature;
                    if (suitePart > 0) flags |= mfSuite;
                    addMove (pick, flags, suitePart == 2 ? baseAmount * 0.6f : baseAmount);
                }
            }

            if (! body)
            {
                int bigOnes = 0;
                for (const auto& c : chosen) if (effect (c.effect).size >= 3) ++bigOnes;
                lastBusy = chosen.size() >= 2 || bigOnes > 0;
                for (const auto& c : chosen) { lastByKind[s.kind] = c.effect; recent.push_back (c.effect); }
                while (recent.size() > 4) recent.erase (recent.begin());
            }
            for (auto& c : chosen) plan.moves.push_back (c);
        }

        // at zero spice this track only plays what you've locked or set to Always
        const auto quiet = spice < 0.01f;

        // ---- the fake drop ----
        if (plan.fakeDrop >= 0 && ! quiet)
            for (const auto& s : slots)
                if (s.kind == sDropStart && s.dropOrdinal == plan.fakeDrop)
                {
                    Move mv; mv.startQ = (float) s.startQ; mv.endQ = (float) (s.startQ + bar); mv.effect = 34; mv.amount = 1.0f;
                    mv.key = s.key; mv.slot = s.kind; mv.seed = mix (seed, 34u);
                    // the drop arrives a bar late: this track's other drop-start moves wait for it
                    for (auto& o : plan.moves)
                        if (o.slot == sDropStart && o.key == s.key) { o.startQ += (float) bar; o.endQ += (float) bar; }
                    plan.moves.push_back (mv);
                }

        // ---- the countdown and the sweep handoff (song-wide decisions; each role has its place) ----
        for (const auto& s : slots)
        {
            if (quiet) break;
            if (s.kind == sBuildEnd && s.next == suite::SectionType::Drop && mode (100) != mNever && in.role != 1 && ! in.suiteActive)
            {
                const auto go = mode (100) == mAlways || unit (mix (in.songSeed, 100u + (uint32_t) s.dropOrdinal)) < 0.45f * juce::jlimit (0.0f, 2.0f, in.songSpice);
                if (go)
                {
                    const auto beat = in.role == 3 ? 0 : in.role == 4 ? 1 : in.role == 5 || in.role == 6 ? 2 : 3;
                    Move mv; mv.startQ = (float) (s.endQ - 4.0 + beat); mv.endQ = mv.startQ + 1.0f; mv.effect = 100; mv.amount = 1.0f;
                    mv.key = s.key ^ 100u; mv.slot = s.kind; mv.seed = mix (seed, 100u);
                    plan.moves.push_back (mv);
                }
            }
            if (s.kind == sBuildBody && mode (103) != mNever && in.role != 1 && in.role != 2)
            {
                const auto go = mode (103) == mAlways || unit (mix (in.songSeed, 103u + (uint32_t) s.section)) < 0.4f * juce::jlimit (0.0f, 2.0f, in.songSpice);
                if (go)
                {
                    const auto slot = in.role == 4 ? 0 : in.role == 5 || in.role == 6 ? 1 : in.role == 3 ? 2 : 3;
                    const auto len = (s.endQ - s.startQ) / 4.0;
                    Move mv; mv.startQ = (float) (s.startQ + slot * len); mv.endQ = (float) (s.startQ + (slot + 1) * len); mv.effect = 103; mv.amount = 1.0f;
                    mv.key = s.key ^ 103u; mv.slot = s.kind; mv.seed = mix (seed, 103u);
                    plan.moves.push_back (mv);
                }
            }
        }

        // ---- contagion: a lead or vocal's stutter in a build spreads here, a bar or more later ----
        if (! quiet && on (116) && (in.role == 3 || in.role == 4 || in.role == 7 || in.role == 0))
            for (const auto& s : slots)
            {
                if (s.kind != sBuildBody) continue;
                double leader = 1.0e9;
                for (const auto& o : in.others)
                    if (o.role == 5 || o.role == 6)
                        for (const auto& mv : o.moves)
                            if (mv.effect >= 1 && mv.effect <= numEffects && effect (mv.effect).family == fRepeat && mv.startQ < s.endQ && mv.endQ > s.startQ)
                                leader = juce::jmin (leader, (double) juce::jmax ((float) s.startQ, mv.startQ));
                if (leader > 1.0e8) continue;
                const auto lag = (in.role == 4 ? 1.0 : in.role == 3 ? 2.0 : 3.0) * bar;
                if (leader + lag >= s.endQ - bar) continue;
                Move mv; mv.startQ = (float) (leader + lag); mv.endQ = (float) s.endQ; mv.effect = 116; mv.amount = juce::jlimit (0.3f, 1.4f, 0.6f + 0.3f * spice);
                mv.key = s.key ^ 116u; mv.slot = s.kind; mv.seed = mix (seed, 116u);
                plan.moves.push_back (mv);
            }

        // ---- call and response: answer the lead's or vocal's gestures a bar later ----
        if (! quiet && on (93) && in.role != 1 && in.role != 2 && ! in.suiteActive)
        {
            for (const auto& o : in.others)
            {
                if (! (o.role == 5 || (o.role == 6 && in.role != 6))) continue;
                for (const auto& om : o.moves)
                {
                    if (om.effect < 1 || om.effect > numEffects) continue;
                    if ((om.slot & (sPhraseEnd | sSectionEnd)) == 0) continue;
                    const auto fam = effect (om.effect).family;
                    if (! (fam == fRepeat || fam == fReverse || fam == fGrain || fam == fPitch || fam == fTape || fam == fStereo)) continue;
                    if (unit (mix (seed, om.key ^ 93u)) > 0.45f * spice) continue;
                    const auto s0 = om.startQ + (float) bar, e0 = juce::jmin (om.endQ + (float) bar, s0 + (float) bar);
                    if (std::any_of (plan.moves.begin(), plan.moves.end(), [&] (const Move& m)
                                     { return (m.slot & bodySlots) == 0 && m.startQ < e0 && m.endQ > s0; }))
                        continue;
                    int pick = 0;
                    for (const auto& e : effects())
                        if (e.family == fam && (e.slots & (sPhraseEnd | sSectionEnd)) && (e.roles & rb) && mode (e.id) != mNever && needsMet (e, in))
                        { pick = e.id; if ((mix (seed, (uint32_t) e.id ^ om.key) & 1u) == 0u) break; }
                    if (pick == 0) continue;
                    Move mv; mv.startQ = s0; mv.endQ = e0; mv.effect = pick; mv.amount = 0.8f; mv.key = om.key ^ 0x93u; mv.slot = sPhraseEnd; mv.flags = mfAnswer;
                    mv.seed = mix (seed, om.key);
                    plan.moves.push_back (mv);
                }
            }
        }

        // the director's silences: nothing plays into them
        if (in.suiteActive && ! in.suiteSilences.empty())
        {
            std::vector<Move> kept;
            for (auto mv : plan.moves)
            {
                bool drop = false;
                if ((mv.slot & bodySlots) == 0)
                    for (const auto& [a, b] : in.suiteSilences)
                        if (mv.startQ < b && mv.endQ > a)
                        {
                            if (mv.startQ < a - 0.05) mv.endQ = (float) a;
                            else drop = true;
                        }
                if (! drop) kept.push_back (mv);
            }
            plan.moves = std::move (kept);
        }
        std::stable_sort (plan.moves.begin(), plan.moves.end(), [] (const Move& a, const Move& b) { return a.startQ < b.startQ; });
        if (plan.moves.size() > 256) plan.moves.resize (256);
        return plan;
    }

    /** Where a moment is, in words: "bar 33 (build end)". */
    inline juce::String slotName (uint32_t kind)
    {
        switch (kind)
        {
            case sBuildBody: return "build"; case sBuildEnd: return "build end"; case sDropStart: return "drop"; case sDropBody: return "drop";
            case sBreakStart: return "breakdown start"; case sBreakPeak: return "breakdown peak"; case sBreakBody: return "breakdown";
            case sPhraseEnd: return "phrase end"; case sSectionEnd: return "section end"; case sOutro: return "outro"; case sFinalDrop: return "final drop";
            case sKeyChange: return "key change"; case sGrooveBody: return "groove"; case sIntoBreak: return "into the breakdown"; case sFinalEnd: return "final phrase";
            case sBuildStart: return "build start"; case sSong: return "whole song"; default: return "";
        }
    }
}
