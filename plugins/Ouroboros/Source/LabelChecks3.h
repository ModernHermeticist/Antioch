#pragma once

// The third set of label-ready checks: the sounds themselves (kick, lead, static pads, the bass on
// small speakers, claps, baked-in noise), the arrangement in detail (the arc against the label's,
// every section change covered, phrase ends, the intro building, the ramp into each drop, the
// breakdown's payoff, the drop's first bars and length, repetition, the outro mirroring the
// intro, density), the vocal (tuning, timing, clarity, sibilance, steadiness, throws), premaster
// dynamics, a simulated master, the low end note by note, space and stereo over time, the top
// end, groove, harmony, production hygiene and render safety. Included at the end of
// LabelReady.h; evaluate() calls checks3(). Each check that an autopilot switch fixes names it in
// `action` ("autopilot:<feature>"); LabelAutoFix.h runs those with the switch on.

namespace lab
{
    inline void checks3 (const Inputs& in, const Profile& P, std::vector<Check>& out)
    {
        using namespace detail;
        namespace AP = suite::Autopilot;
        auto add = [&out] (int area, const juce::String& name, bool measured, float score, const juce::String& detail, const juce::String& explain = {}, int bar = -1,
                           bool blocking = false, suite::Fix fix = suite::Fix::None, uint32_t data = 0, bool safe = false, const juce::String& action = {})
        {
            Check c;
            c.area = area; c.name = name; c.measured = measured; c.score = juce::jlimit (0.0f, 1.0f, score); c.detail = detail; c.explain = explain;
            c.bar = bar; c.blocking = blocking; c.fix = fix; c.fixData = data; c.safeFix = safe; c.action = action;
            out.push_back (c);
        };
        auto feature = [] (AP::Feature f) { return "autopilot:" + juce::String ((int) f); };
        const auto& S = in.sections;
        const auto beatMs = 60000.0f / (float) juce::jlimit (60.0, 200.0, in.bpm);
        const auto noTraits = juce::String ("put Quintessence on the tracks (it measures each one)");
        auto heard = [] (const suite::TrackTraits::Traits& t) { return t.outRmsDb > -60.0f; };
        auto loudestOf = [&in, &heard] (int role) -> const suite::TrackTraits::Traits*
        {
            const suite::TrackTraits::Traits* best = nullptr;
            for (const auto& t : in.traits) if (t.role == role && heard (t) && (best == nullptr || t.outRmsDb > best->outRmsDb)) best = &t;
            return best;
        };
        auto bandsOf = [&in] (const suite::TrackTraits::Traits& t, Bands& b)
        {
            for (const auto& r : in.tracks) if (r.track == t.trackName()) { b = r.bands; return true; }
            return false;
        };
        auto sectionAtBar = [&in] (int bar)
        {
            int idx = -1;
            for (int i = 0; i < in.map.count; ++i) if (in.map.sections[(size_t) i].startBar <= bar) idx = i;
            return idx;
        };
        std::vector<int> drops, breaks, builds;
        for (const auto& s : S) { if (s.type == T::Drop) drops.push_back (s.index); if (s.type == T::Breakdown) breaks.push_back (s.index); if (s.type == T::Build) builds.push_back (s.index); }
        int heardBars = 0; for (const auto& b : in.bars) heardBars += b.heard ? 1 : 0;
        const auto songHeard = heardBars >= 16;

        // ===================== A. the sounds =====================
        // 1. the kick: click against body, its length against the tempo, its sub tail
        if (const auto* k = loudestOf (1))
        {
            Bands b {};
            if (bandsOf (*k, b))
            {
                const auto click = powerSumDb (b, 6, 7), body = powerSumDb (b, 1, 2), sub = b[0];
                const auto ratio = click - body;
                const auto lengthBeats = k->decayCount >= 3 ? 3.0f * k->decayMs / beatMs : 0.5f;
                const auto sc = juce::jmin (juce::jmin (ramp (ratio, -14.0f, -24.0f), ramp (ratio, -6.0f, 0.0f)), juce::jmin (ramp (lengthBeats, 0.7f, 1.1f), ramp (sub - body, -4.0f, -12.0f)));
                add (aMids, "Kick quality", true, sc, trackLabel (*k) + ": click " + db (ratio) + " under the body, rings " + juce::String (lengthBeats, 2) + " beats, sub " + db (sub - body),
                     ratio < -14.0f ? juce::String ("more click (2-4 kHz) so it cuts through") : ratio > -6.0f ? juce::String ("less click, more body") : lengthBeats > 0.7f ? juce::String ("a shorter tail so the bass has room")
                                                                                                                                                    : juce::String ("more sub under the body"),
                     -1, false, suite::Fix::MasterTone, suite::packMasterTone (k->id, ratio < -14.0f ? 7 : ratio > -6.0f ? 7 : 0, ratio < -14.0f ? 1.0f : ratio > -6.0f ? -1.0f : (sub - body < -4.0f ? 1.0f : 0.0f)), true,
                     feature (AP::kickShapeF));
            }
        }
        else add (aMids, "Kick quality", false, 1.0f, in.traits.empty() ? noTraits : juce::String ("no kick track heard (a track named Kick, or role set)"));
        // 2. the lead's timbre: presence and air against the label's drops
        if (const auto* l = loudestOf (5))
        {
            Bands b {};
            if (bandsOf (*l, b))
            {
                const auto sh = shape (b), lab = P.toneOf (T::Drop);
                const auto pres = (sh[6] + sh[7]) * 0.5f - (sh[4] + sh[5]) * 0.5f, labPres = (lab[6] + lab[7]) * 0.5f - (lab[4] + lab[5]) * 0.5f;
                const auto d = pres - labPres;
                add (aMids, "Lead timbre", true, ramp (std::abs (d), 3.0f, 9.0f), trackLabel (*l) + ": presence " + db (d) + " against the label's drops",
                     d < 0.0f ? juce::String ("brighter: more 2-4 kHz on the lead") : juce::String ("softer: less 2-4 kHz on the lead"), -1, false,
                     suite::Fix::MasterTone, suite::packMasterTone (l->id, 6, d < 0.0f ? 1.0f : -1.0f), true, feature (AP::leadTimbreF));
            }
        }
        // 3. static pads
        {
            const suite::TrackTraits::Traits* still = nullptr;
            for (const auto& t : in.traits) if (t.role == 4 && heard (t) && t.movementCount >= 20 && (still == nullptr || t.movementDb < still->movementDb)) still = &t;
            if (still != nullptr)
                add (aMids, "Pads that move", true, ramp (still->movementDb, 1.2f, 0.3f), trackLabel (*still) + " changes " + juce::String (still->movementDb, 1) + " dB per tenth of a second (its tone)",
                     "a slow filter movement over the phrase (the autopilot: pad motion)", -1, false, suite::Fix::None, 0, false, feature (AP::padMotionF));
            else add (aMids, "Pads that move", false, 1.0f, in.traits.empty() ? noTraits : juce::String ("no pad heard long enough"));
        }
        // 4. the bass on small speakers
        if (const auto* b = loudestOf (2))
        {
            Bands bb {};
            if (bandsOf (*b, bb))
            {
                const auto d = powerSumDb (bb, 4, 5) - powerSumDb (bb, 1, 2);
                add (aTranslation, "The bass's harmonics", true, ramp (d, -16.0f, -26.0f), trackLabel (*b) + ": 500 Hz-1 kHz " + db (d) + " under its lows",
                     "harmonics on the bass so phones hear its notes (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::harmonicsF));
            }
        }
        // 5. clap and snare transients
        {
            const suite::TrackTraits::Traits* hit = nullptr;
            for (const auto& t : in.traits)
                if (t.role == 3 && heard (t) && (t.trackName().toLowerCase().contains ("clap") || t.trackName().toLowerCase().contains ("snare") || t.trackName().toLowerCase().contains ("snr"))) if (hit == nullptr || t.outRmsDb > hit->outRmsDb) hit = &t;
            if (hit == nullptr) hit = loudestOf (3);
            if (hit != nullptr)
                add (aMids, "Claps and snares", true, juce::jmin (ramp (hit->crestDb, 11.0f, 7.0f), ramp (hit->crestDb, 20.0f, 25.0f)), trackLabel (*hit) + ": crest " + juce::String (hit->crestDb, 1) + " dB",
                     hit->crestDb < 11.0f ? juce::String ("more attack (the transient shaper)") : juce::String ("a softer attack (the transient shaper)"), -1, false,
                     suite::Fix::TrackPunch, suite::packMasterTone (hit->id, 0, hit->crestDb < 11.0f ? 1.0f : -1.0f), true, feature (AP::clapTransientF));
        }
        // 6. noise and aliasing baked into samples
        {
            const suite::TrackTraits::Traits* noisy = nullptr; float worst = -200.0f;
            for (const auto& t : in.traits)
            {
                if (! heard (t) || t.noiseFloorDb < -99.0f) continue;
                const auto gap = t.noiseFloorDb - t.outRmsDb;   // the quietest moments against its level
                if (gap > worst && t.role != 4 && t.role != 7) { worst = gap; noisy = &t; }
            }
            if (noisy != nullptr)
                add (aHygiene, "Noise in the samples", true, ramp (worst, -45.0f, -25.0f), trackLabel (*noisy) + ": its quietest moments only " + db (-worst) + " under its level",
                     "a gate on the noise floor and less of the very top (one click, or the autopilot)", -1, false,
                     suite::Fix::MasterTone, suite::packMasterTone (noisy->id, 9, -1.5f), true, feature (AP::hygieneF));
        }

        // ===================== B, L. arrangement =====================
        // 7. the arc against the label's
        if (songHeard && ! P.refs.empty())
        {
            constexpr int points = 16;
            std::array<float, points> mine {}, theirs {};
            std::array<int, points> nTheirs {};
            auto arcOf = [] (const std::vector<SectionStat>& secs, int total, std::array<float, points>& arc, std::array<int, points>& n)
            {
                if (total <= 0) return;
                std::vector<float> lv;
                for (const auto& s : secs) if (s.heard()) lv.push_back (s.db);
                if (lv.empty()) return;
                std::sort (lv.begin(), lv.end());
                const auto mid = lv[lv.size() / 2];
                for (const auto& s : secs)
                    if (s.heard())
                        for (int p = 0; p < points; ++p)
                        {
                            const auto barAt = (float) p / (float) points * (float) total + 1.0f;
                            if (barAt >= (float) s.startBar && barAt < (float) (s.startBar + s.bars)) { arc[(size_t) p] += s.db - mid; ++n[(size_t) p]; }
                        }
            };
            std::array<int, points> nMine {};
            arcOf (S, in.map.totalBars, mine, nMine);
            for (const auto& r : P.refs)
            {
                int total = 0; for (const auto& s : r.sections) total = juce::jmax (total, s.startBar - 1 + s.bars);
                arcOf (r.sections, total, theirs, nTheirs);
            }
            float rms = 0.0f; int n = 0, worstP = -1; float worstD = 0.0f;
            for (int p = 0; p < points; ++p)
                if (nMine[(size_t) p] > 0 && nTheirs[(size_t) p] > 0)
                {
                    const auto d = mine[(size_t) p] / (float) nMine[(size_t) p] - theirs[(size_t) p] / (float) nTheirs[(size_t) p];
                    rms += d * d; ++n;
                    if (std::abs (d) > std::abs (worstD)) { worstD = d; worstP = p; }
                }
            if (n > 4)
            {
                rms = std::sqrt (rms / (float) n);
                const auto bar = worstP >= 0 ? 1 + worstP * in.map.totalBars / points : -1;
                add (aReference, "The arc against the label's", true, ramp (rms, 2.5f, 6.0f), juce::String (rms, 1) + " dB from the label's average arc; furthest at bar " + juce::String (bar) + " (" + db (worstD) + ")",
                     "that part of the song louder or quieter, as the label's tracks are there", bar);
            }
        }
        // 8. every section change covered by a moment
        if (in.map.count > 1)
        {
            int bare = 0, firstBare = -1;
            for (int i = 1; i < in.map.count; ++i)
            {
                bool covered = false;
                if (in.plan != nullptr)
                    for (const auto& m : in.plan->moments) if (m.section == i && m.hero != suite::director::pNone) covered = true;
                if (! covered && (int) in.bars.size() >= in.map.sections[(size_t) i].startBar && in.map.sections[(size_t) i].startBar >= 3)
                {
                    const auto b = in.map.sections[(size_t) i].startBar - 2;   // the bar before the change
                    const auto& x = in.bars[(size_t) b]; const auto& y = in.bars[(size_t) juce::jmax (0, b - 2)];
                    covered = x.heard && y.heard && (powerSumDb (x.band, 7, numBands - 1) - powerSumDb (y.band, 7, numBands - 1) > 2.0f || x.onsets > y.onsets * 1.5f);
                }
                if (! covered) { ++bare; if (firstBare < 0) firstBare = in.map.sections[(size_t) i].startBar; }
            }
            add (aAR, "Every section change marked", in.plan != nullptr || songHeard, ramp ((float) bare, 0.0f, 3.0f),
                 bare == 0 ? juce::String ("each change has a riser, impact, fill or move") : juce::String (bare) + " change(s) with nothing on them (first at bar " + juce::String (firstBare) + ")",
                 "a riser, an impact, a reverse or a fill there (the director: more going on)", firstBare, false, suite::Fix::None, 0, false, feature (AP::sectionFxF));
        }
        // 9. phrase ends: something changes every 8 bars
        if (songHeard)
        {
            int ends = 0, changed = 0, firstFlat = -1;
            for (size_t b = 7; b + 1 < in.bars.size(); b += 8)
            {
                const auto& x = in.bars[b]; const auto& y = in.bars[b - 1]; const auto& z = in.bars[b + 1];
                if (! x.heard || ! y.heard) continue;
                ++ends;
                const auto moved = std::abs (x.db - y.db) > 1.0f || std::abs (x.onsets - y.onsets) > 2.0f || std::abs (z.db - x.db) > 1.5f
                                   || std::abs (powerSumDb (x.band, 7, numBands - 1) - powerSumDb (y.band, 7, numBands - 1)) > 1.5f;
                changed += moved ? 1 : 0;
                if (! moved && firstFlat < 0) firstFlat = (int) b + 1;
            }
            if (ends >= 4)
                add (aAR, "Phrase ends change something", true, ramp ((float) changed / (float) ends, 0.8f, 0.4f), juce::String (changed) + " of " + juce::String (ends) + " 8-bar phrase ends do something",
                     "a fill, a crash, a gap in the hats or an arp lift at the end of each phrase", firstFlat, false, suite::Fix::None, 0, false, feature (AP::phraseFillsF));
        }
        // 10. the intro builds: something new every 8 bars
        if (! S.empty() && S[0].type == T::Intro && S[0].heardBars >= 16)
        {
            int steps = 0, grew = 0;
            for (int b = S[0].startBar - 1 + 8; b + 8 <= S[0].startBar - 1 + S[0].bars && b + 8 <= (int) in.bars.size(); b += 8)
            {
                float a = 0, c = 0, oa = 0, oc = 0;
                for (int k = 0; k < 8; ++k) { a += in.bars[(size_t) (b - 8 + k)].db; c += in.bars[(size_t) (b + k)].db; oa += in.bars[(size_t) (b - 8 + k)].onsets; oc += in.bars[(size_t) (b + k)].onsets; }
                ++steps; grew += (c - a) / 8.0f > 0.5f || oc > oa * 1.2f ? 1 : 0;
            }
            if (steps > 0)
                add (aAR, "The intro builds", true, ramp ((float) grew / (float) steps, 0.9f, 0.3f), juce::String (grew) + " of " + juce::String (steps) + " 8-bar steps add something",
                     "a new element every 8 bars (Correspondence writes the entries)", S[0].startBar, false, suite::Fix::None, 0, false, feature (AP::arrangeEntriesF));
        }
        // 11. the ramp into each drop: a curve, not flat and then a jump
        for (auto d : drops)
        {
            const auto start = S[(size_t) d].startBar - 1;
            if (start < 5 || start > (int) in.bars.size()) continue;
            std::array<float, 4> lv {};
            bool ok = true;
            for (int k = 0; k < 4; ++k) { const auto& x = in.bars[(size_t) (start - 4 + k)]; ok = ok && x.heard; lv[(size_t) k] = x.db + 0.5f * powerSumDb (x.band, 7, numBands - 1) * 0.1f; }
            if (! ok) continue;
            const auto rise = lv[3] - lv[0], early = lv[2] - lv[0];
            const auto curve = rise > 0.5f ? early / rise : 0.0f;   // the share of the rise in the first three bars
            add (aAR, "The ramp into the drop at bar " + juce::String (start + 1), true, juce::jmin (ramp (rise, 1.5f, 0.0f), ramp (curve, 0.3f, 0.05f)),
                 "it rises " + db (rise) + " over the last 4 bars, " + juce::String (juce::roundToInt (curve * 100.0f)) + "% of it before the last bar",
                 "a riser, a roll and a filter that climb through the last bars (the director)", start + 1, false, suite::Fix::BuildRise, 0, true, feature (AP::preDropCurveF));
            break;
        }
        // 12. the breakdown's payoff: its length against the drop it leads to
        for (auto b : breaks)
        {
            const auto& s = S[(size_t) b];
            if (! s.heard() || b + 1 >= (int) S.size()) continue;
            int next = -1; for (size_t j = (size_t) b + 1; j < S.size() && next < 0; ++j) if (S[j].type == T::Drop) next = (int) j;
            if (next < 0 || ! S[(size_t) next].heard()) continue;
            const auto payoff = S[(size_t) next].db - s.db;
            const auto perBar = payoff / (float) juce::jmax (8, s.bars) * 32.0f;   // dB of payoff per 32 bars waited
            add (aAR, "The breakdown pays off (bar " + juce::String (s.startBar) + ")", true, ramp (perBar, 8.0f, 3.0f),
                 juce::String (s.bars) + " bars for a " + db (payoff) + " drop", s.bars > 32 ? juce::String ("a shorter breakdown, or a bigger drop after it") : juce::String ("a quieter breakdown or a bigger drop"), s.startBar);
            break;
        }
        // 52-53. the drop's first bars and its length
        if (! drops.empty() && ! in.traits.empty())
        {
            const auto d = drops[0];
            int playing = 0, before = 0;
            for (const auto& t : in.traits)
            {
                if (d < suite::TrackTraits::maxSections && t.sectionDb[(size_t) d] > -45.0f) ++playing;
                if (d > 0 && d - 1 < suite::TrackTraits::maxSections && t.sectionDb[(size_t) d - 1] > -45.0f) ++before;
            }
            add (aAR, "The drop arrives full", true, ramp ((float) (playing - before), 3.0f, 0.0f), juce::String (playing) + " parts in the drop against " + juce::String (before) + " just before it",
                 "the drop brings in at least three parts the build didn't have", S[(size_t) d].startBar);
        }
        if (! drops.empty())
        {
            std::vector<std::pair<float, float>> lens;
            for (const auto& r : P.refs) for (const auto& s : r.sections) if (s.type == T::Drop) lens.push_back ({ (float) s.bars, Profile::weightOf (r) });
            const auto st = lens.empty() ? Stat {} : statOf (lens);
            const auto norm = st.valid() ? st : [] { Stat s; s.median = 24.0f; s.lo = 16.0f; s.hi = 32.0f; s.n = 1; return s; }();
            const auto& s = S[(size_t) drops[0]];
            add (aReference, "Drop length", true, fromOutside (norm.outside ((float) s.bars)), juce::String (s.bars) + " bars (the label: " + norm.text (0) + ")",
                 s.bars > norm.hi ? "cut " + juce::String (s.bars - (int) norm.median) + " bars from the drop in FL's playlist (a plugin can't cut it for you)" : juce::String ("a longer drop"), s.startBar);
        }
        // 54. repetition fatigue: the same 8 bars over and over
        if (songHeard)
        {
            int run = 0, worstRun = 0, worstAt = -1;
            auto sameBlock = [&in] (size_t a, size_t b)
            {
                float d = 0.0f;
                for (size_t k = 0; k < 8; ++k)
                {
                    const auto& x = in.bars[a + k]; const auto& y = in.bars[b + k];
                    if (! x.heard || ! y.heard) return false;
                    d += std::abs (x.db - y.db) + 0.5f * std::abs (x.onsets - y.onsets) + distance (x.band, y.band);
                }
                return d / 8.0f < 1.2f;
            };
            for (size_t b = 8; b + 8 <= in.bars.size(); b += 8)
            {
                run = sameBlock (b - 8, b) ? run + 1 : 0;
                if (run > worstRun) { worstRun = run; worstAt = (int) b - 8 * (run - 1) + 1; }
            }
            add (aAR, "No 8 bars repeated too often", true, ramp ((float) worstRun, 1.0f, 3.0f), worstRun == 0 ? juce::String ("no 8-bar block repeats unchanged") : juce::String (worstRun + 1) + " identical 8-bar blocks in a row (from bar " + juce::String (worstAt) + ")",
                 "a variation every 16 bars: a phrase turnaround, ghost notes, an arp change (Correspondence)", worstAt, false, suite::Fix::None, 0, false, feature (AP::loopVariationF));
        }
        // 55. room before each drop (a breath without silence)
        for (auto d : drops)
        {
            const auto start = S[(size_t) d].startBar - 1;
            if (start < 3 || start >= (int) in.bars.size()) continue;
            const auto& last = in.bars[(size_t) start - 1]; const auto& prev = in.bars[(size_t) start - 2];
            if (! last.heard || ! prev.heard) continue;
            const auto room = prev.db - last.db + 0.5f * (powerSumDb (prev.band, 0, 1) - powerSumDb (last.band, 0, 1));
            add (aAR, "Room before the drop", true, ramp (room, 1.0f, -2.0f), "the last bar before bar " + juce::String (start + 1) + " is " + db (-room) + " against the one before (with the lows)",
                 "pull the lows or the level back in the last bar (a filter opening into the drop; not a full silence)", start + 1, false, suite::Fix::None, 0, false, "entrance:2");
            break;
        }
        // 56. the outro mirrors the intro
        if (S.size() >= 2 && S.front().type == T::Intro && S.back().type == T::Outro && ! in.traits.empty())
        {
            const auto first = S.front().index, last = S.back().index;
            int both = 0, either = 0;
            for (const auto& t : in.traits)
            {
                if (last >= suite::TrackTraits::maxSections) break;
                const auto a = t.sectionDb[(size_t) first] > -45.0f, b = t.sectionDb[(size_t) last] > -45.0f;
                both += a && b ? 1 : 0; either += a || b ? 1 : 0;
            }
            const auto falls = S.back().firstDb - S.back().lastDb;
            if (either > 0)
                add (aAR, "The outro mirrors the intro", true, juce::jmin (ramp ((float) both / (float) either, 0.6f, 0.2f), ramp (falls, 3.0f, 0.0f)),
                     juce::String (both) + " of " + juce::String (either) + " parts in both; it falls " + db (falls),
                     "the outro takes away what the intro brought in, in reverse (Correspondence writes the outro)", S.back().startBar, false, suite::Fix::None, 0, false, feature (AP::arrangeEntriesF));
        }
        // 57. contrast map: every neighbouring pair
        if (S.size() >= 3)
        {
            juce::StringArray pairs; float weakest = 100.0f; int weakBar = -1;
            for (size_t i = 1; i < S.size(); ++i)
            {
                if (! S[i].heard() || ! S[i - 1].heard()) continue;
                const auto c = std::abs (S[i].db - S[i - 1].db) + distance (S[i].band, S[i - 1].band) + 0.3f * std::abs (S[i].onsetsPerBar - S[i - 1].onsetsPerBar);
                pairs.add (juce::String (sectionName (S[i - 1].type)) + ">" + sectionName (S[i].type) + " " + juce::String (c, 1));
                if (c < weakest) { weakest = c; weakBar = S[i].startBar; }
            }
            if (! pairs.isEmpty())
                add (aAR, "Contrast map", true, ramp (weakest, 3.0f, 1.0f), pairs.joinIntoString (", "), "the weakest change (at bar " + juce::String (weakBar) + ") needs more difference: level, tone or density", weakBar);
        }
        // 58. density: more parts in the drops than around them
        if (! in.traits.empty() && ! drops.empty())
        {
            auto countIn = [&in] (int i) { int n = 0; for (const auto& t : in.traits) if (i < suite::TrackTraits::maxSections && t.sectionDb[(size_t) i] > -45.0f) ++n; return n; };
            int dropN = 0, otherMax = 0;
            for (const auto& s : S) { const auto n = countIn (s.index); if (s.type == T::Drop) dropN = juce::jmax (dropN, n); else otherMax = juce::jmax (otherMax, n); }
            add (aAR, "Density peaks in the drops", dropN > 0, ramp ((float) (dropN - otherMax), 1.0f, -2.0f), juce::String (dropN) + " parts at most in a drop, " + juce::String (otherMax) + " elsewhere",
                 "fewer parts outside the drops, or one more layer in them (the density manager thins crowded sections)", -1, false, suite::Fix::None, 0, false, feature (AP::densityF));
        }

        // ===================== C. vocal =====================
        if (! in.vocal.empty())
        {
            // 13. tuning
            int off = 0, n = 0; float worstC = 0.0f; int worstBar = -1;
            for (size_t i = 0; i < in.vocal.size() && i < in.vocalCents.size(); ++i)
            {
                ++n;
                const auto c = in.vocalCents[i];
                if (std::abs (c) > 25.0f) { ++off; if (std::abs (c) > std::abs (worstC)) { worstC = c; worstBar = (int) (in.vocal[i].start / in.barQ) + 1; } }
            }
            if (n > 0)
                add (aHygiene, "Vocal tuning", true, ramp ((float) off / (float) n, 0.05f, 0.25f), juce::String (off) + " of " + juce::String (n) + " sung notes over 25 cents off (worst " + juce::String (juce::roundToInt (worstC)) + " at bar " + juce::String (worstBar) + ")",
                     "correct those notes in a pitch editor (the suite has no pitch correction; Solve et Coagula's vocal check lists them)", worstBar);
            // 14. timing
            std::vector<float> offs;
            for (const auto& v : in.vocal) { const auto q = v.start * 4.0f; offs.push_back ((q - std::round (q)) / 4.0f * beatMs); }
            if (offs.size() >= 8)
            {
                auto sorted = offs; std::sort (sorted.begin(), sorted.end());
                const auto median = sorted[sorted.size() / 2];
                const auto* vt = loudestOf (6);
                const auto early = median < -5.0f && vt != nullptr;
                add (aHygiene, "Vocal timing", true, ramp (std::abs (median), 15.0f, 40.0f), "the vocal sits " + juce::String (juce::roundToInt (std::abs (median))) + " ms " + (median > 0 ? "late" : "early") + " on average",
                     early ? juce::String ("delayed onto the grid by its Quintessence (one click, or the autopilot)") : juce::String ("slide the vocal clip earlier in FL (a plugin can't play it before it arrives)"),
                     -1, false, early ? suite::Fix::VocalDelay : suite::Fix::None, early ? suite::packMasterTone (vt->id, 0, juce::jmin (60.0f, -median)) : 0u, early,
                     early ? feature (AP::vocalTimingF) : juce::String());
            }
        }
        if (const auto* v = loudestOf (6))
        {
            Bands vb {};
            if (bandsOf (*v, vb))
            {
                // 15. clarity: the vocal's 2-4 kHz over the pads and leads there
                float others = -200.0f; uint32_t culprit = 0; Bands cb {};
                for (const auto& t : in.traits)
                    if ((t.role == 4 || t.role == 5) && heard (t) && bandsOf (t, cb))
                        if (const auto e = powerSumDb (cb, 6, 7); e > others) { others = e; culprit = t.id; }
                if (others > -150.0f)
                {
                    const auto margin = powerSumDb (vb, 6, 7) - others;
                    add (aMids, "The vocal is clear", true, ramp (margin, 3.0f, -3.0f), "its 2-4 kHz " + db (margin) + " over the loudest pad or lead there",
                         "the pads and leads dip at 2-4 kHz while the vocal sings (one click, or the autopilot)", -1, false,
                         suite::Fix::Carve, suite::packCarve (culprit, 6, 1, false), true, feature (AP::vocalClarityF));
                }
                // 16. sibilance
                const auto sib = vb[8] - (vb[6] + vb[7]) * 0.5f;
                add (aMids, "Sibilance", true, ramp (sib, -3.0f, 3.0f), "8 kHz " + db (sib) + " against the vocal's presence", "more de-essing on the vocal", -1, false,
                     suite::Fix::MasterTone, suite::packMasterTone (v->id, 8, -1.0f), true, feature (AP::sibilanceF));
            }
            // 17. steady from section to section
            std::vector<float> margins; int lowI = -1; float lowM = 100.0f;
            for (const auto& s : S)
                if (s.heard() && s.index < suite::TrackTraits::maxSections && v->sectionDb[(size_t) s.index] > -60.0f)
                {
                    const auto m = v->sectionDb[(size_t) s.index] - s.db;
                    margins.push_back (m);
                    if (m < lowM) { lowM = m; lowI = s.index; }
                }
            if (margins.size() >= 2)
            {
                const auto spread = *std::max_element (margins.begin(), margins.end()) - *std::min_element (margins.begin(), margins.end());
                add (aBalance, "The vocal stays steady", true, ramp (spread, 3.0f, 7.0f), "it moves " + juce::String (spread, 1) + " dB against the mix between sections",
                     "ride it up where it sinks (one click, or the autopilot)", S[(size_t) lowI].startBar, false,
                     spread > 3.0f ? suite::Fix::SectionTrim : suite::Fix::None, suite::packSectionTrim (v->id, (int) S[(size_t) lowI].type, juce::jmin (3.0f, spread - 2.0f)), true, feature (AP::vocalSteadyF));
            }
            // 18. throws where the vocal rests
            add (aMids, "Vocal throws", v->decayCount >= 3, ramp (v->decayMs * 3.0f / beatMs, 1.0f, 0.3f), "the vocal's tails ring " + juce::String (v->decayMs * 3.0f / beatMs, 1) + " beats",
                 "a delay or reverb throw at the end of each vocal phrase (Antioch: throws at vocal ends, one click)", -1, false, suite::Fix::ThrowsAtVocal, 0, true, feature (AP::throwsF));
        }

        // ===================== D, E. premaster dynamics and a simulated master =====================
        if (songHeard)
        {
            // 19. crest per section
            float worstCrest = 100.0f; int worstBar = -1;
            for (const auto& s : S)
            {
                if (! s.heard() || s.type == T::Intro || s.type == T::Outro) continue;
                float pk = -100.0f;
                for (int b = s.startBar - 1; b < s.startBar - 1 + s.bars && b < (int) in.bars.size(); ++b) pk = juce::jmax (pk, in.bars[(size_t) b].peakDb);
                if (pk > -99.0f && pk - s.db < worstCrest) { worstCrest = pk - s.db; worstBar = s.startBar; }
            }
            if (worstBar > 0)
                add (aHygiene, "Room for the mastering engineer", true, ramp (worstCrest, 9.0f, 5.0f), "the most squashed section has " + juce::String (worstCrest, 1) + " dB between its peaks and its level",
                     "let it breathe: less glue and multiband there (one click)", worstBar, false, suite::Fix::LessSquash, 0, true, feature (AP::squashF));
            // 21. where the master gets closest to -6 dBFS, and what does it
            float pk = -100.0f; int pkBar = -1;
            for (size_t b = 0; b < in.bars.size(); ++b) if (in.bars[b].heard && in.bars[b].peakDb > pk) { pk = in.bars[b].peakDb; pkBar = (int) b + 1; }
            if (pkBar > 0)
            {
                const auto sec = sectionAtBar (pkBar);
                const suite::TrackTraits::Traits* who = nullptr;
                for (const auto& t : in.traits)
                    if (sec >= 0 && sec < suite::TrackTraits::maxSections && (who == nullptr || t.sectionPeakDb[(size_t) sec] > who->sectionPeakDb[(size_t) sec])) who = &t;
                add (aHygiene, "The loudest peak", true, ramp (pk, -7.0f, -5.0f), db (pk) + " at bar " + juce::String (pkBar) + (who != nullptr ? " (mostly " + trackLabel (*who) + ")" : juce::String()),
                     "that track down a little in that section, nothing else (one click)", pkBar, false,
                     who != nullptr && pk > -7.0f ? suite::Fix::SectionTrim : suite::Fix::None, who != nullptr && sec >= 0 ? suite::packSectionTrim (who->id, (int) in.map.sections[(size_t) sec].type, -1.0f) : 0u, true,
                     feature (AP::peakTrimF));
            }
            // 23-24. a simulated master: up to -8 dB RMS in the drop through a -1 dBFS ceiling (listen-only, never rendered)
            if (! drops.empty() && S[(size_t) drops[0]].heard())
            {
                const auto& d = S[(size_t) drops[0]];
                const auto gain = -8.0f - d.db;
                float grMax = 0.0f, grSum = 0.0f; int n = 0;
                for (int b = d.startBar - 1; b < d.startBar - 1 + d.bars && b < (int) in.bars.size(); ++b)
                {
                    const auto gr = juce::jmax (0.0f, in.bars[(size_t) b].peakDb + gain + 1.0f);
                    grMax = juce::jmax (grMax, gr); grSum += gr; ++n;
                }
                const auto grMean = n > 0 ? grSum / (float) n : 0.0f;
                const auto sh = shape (d.band);
                const auto harsh = (sh[6] + sh[7]) * 0.5f + 1.0f - (P.toneOf (T::Drop)[6] + P.toneOf (T::Drop)[7]) * 0.5f;   // louder masters sound brighter
                const auto sc = juce::jmin (ramp (grMean, 4.0f, 8.0f), ramp (harsh, 2.0f, 5.0f));
                add (aHygiene, "Mastering stress test", n > 0, sc,
                     "pushed to a club master the drop needs " + juce::String (grMean, 1) + " dB of limiting (" + juce::String (grMax, 1) + " at most); its top end " + db (harsh) + " against the label after it",
                     grMean > 4.0f ? juce::String ("the loudest hits (usually the kick) down a little so the limiter works less") : juce::String ("the 2-4 kHz down a little before it's mastered"),
                     d.startBar, false, grMean > 4.0f ? suite::Fix::LessSquash : suite::Fix::None, 0, true, feature (AP::stressFixF));
            }
            // 25. resonances a mastering EQ would make worse: several tracks ringing at one frequency
            if (in.resonanceHz.size() >= 2)
            {
                int most = 0; float at = 0.0f;
                for (auto f : in.resonanceHz)
                {
                    int c = 0; for (auto g : in.resonanceHz) c += std::abs (std::log2 (g / f)) < 0.08f ? 1 : 0;
                    if (c > most) { most = c; at = f; }
                }
                add (aMids, "Shared resonances", true, ramp ((float) most, 1.0f, 3.0f), juce::String (most) + " track(s) ring around " + juce::String (juce::roundToInt (at)) + " Hz",
                     "one cut shared between them (the autopilot: shared resonance cuts)", -1, false, suite::Fix::None, 0, false, feature (AP::sharedResonanceF));
            }
        }

        // ===================== I. low end, note by note =====================
        if (in.song.valid && in.song.bass.size() >= 8)
        {
            // 37. bass notes ring into the next kick
            int over = 0;
            for (const auto& n : in.song.bass) { const auto nextBeat = std::floor (n.start + 1.0e-4f) + 1.0f; over += n.start + n.length > nextBeat + 0.01f ? 1 : 0; }
            add (aLowEnd, "Bass notes stop before the kick", true, ramp ((float) over / (float) in.song.bass.size(), 0.1f, 0.5f),
                 juce::String (juce::roundToInt (100.0f * (float) over / (float) in.song.bass.size())) + "% of the bass notes ring into the next beat's kick",
                 "bass notes end a 16th before the next kick (Correspondence, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::bassClearKickF));
            // 38. bass notes off the key
            const auto& sc = suite::music::scales()[(size_t) juce::jlimit (0, (int) suite::music::scales().size() - 1, in.song.scale)].steps;
            int outKey = 0;
            for (const auto& n : in.song.bass) { const auto pc = ((n.pitch - in.song.root) % 12 + 12) % 12; outKey += std::find (sc.begin(), sc.end(), pc) == sc.end() ? 1 : 0; }
            add (aLowEnd, "The bass stays in key", true, ramp ((float) outKey / (float) in.song.bass.size(), 0.02f, 0.15f), juce::String (outKey) + " bass note(s) outside the key",
                 "those notes snapped to the nearest note in key (Correspondence, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::bassInKeyF));
            // 39. where the kick and bass collide, bar by bar
            juce::String heat;
            int hot = 0, bars = 0;
            for (int b = 0; b < juce::jmin (in.map.totalBars, 128); ++b)
            {
                int hits = 0;
                for (const auto& n : in.song.bass) if (n.start >= b * 4.0f && n.start < b * 4.0f + 4.0f && std::abs (n.start - std::round (n.start)) < 0.02f) ++hits;
                if (hits == 0) continue;
                ++bars; hot += hits >= 3 ? 1 : 0;
                if (heat.length() < 64) heat << (hits >= 3 ? "#" : hits >= 1 ? "+" : ".");
            }
            if (bars > 0)
                add (aLowEnd, "Kick and bass overlap", true, ramp ((float) hot / (float) bars, 0.1f, 0.6f), juce::String (hot) + " of " + juce::String (bars) + " bass bars put notes on the kicks (" + heat + ")",
                     "the bass dips under each kick (the autopilot: bass dips at the kick)", -1, false, suite::Fix::KickFit, 0, true, feature (AP::kickBassDipF));
        }
        if (const auto* b = loudestOf (2))
            if (b->hitCount >= 8)
                add (aLowEnd, "Bass notes even across the octave", true, ramp (b->hitSpreadDb, 4.0f, 9.0f), trackLabel (*b) + ": its notes spread " + juce::String (b->hitSpreadDb, 1) + " dB",
                     "the bass's notes evened out (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::bassNotesF));
        // 41. rumble in the breakdowns (tracks other than the bass with lows there)
        for (auto bi : breaks)
        {
            if (bi >= suite::TrackTraits::maxSections) continue;
            const suite::TrackTraits::Traits* rumbly = nullptr;
            for (const auto& t : in.traits) if (t.role != 2 && t.role != 1 && t.lowMidDb > t.outRmsDb - 6.0f && t.sectionDb[(size_t) bi] > -50.0f) rumbly = &t;
            add (aLowEnd, "No rumble in the breakdown", ! in.traits.empty(), rumbly == nullptr ? 1.0f : 0.5f,
                 rumbly == nullptr ? juce::String ("nothing but the bass carries lows there") : trackLabel (*rumbly) + " carries lows into the breakdown",
                 "a low cut on everything but the bass in the breakdowns (the autopilot)", S[(size_t) bi].startBar, false, suite::Fix::None, 0, false, feature (AP::breakdownLowCutF));
            break;
        }

        // ===================== J. space and stereo =====================
        {
            // 42. reverb returns carrying lows
            const suite::TrackTraits::Traits* wet = nullptr;
            for (const auto& t : in.traits)
            {
                const auto n = t.trackName().toLowerCase();
                if ((n.contains ("verb") || n.contains ("return") || n.contains ("delay") || n.contains ("send")) && heard (t) && t.lowMidDb > t.outRmsDb - 10.0f) wet = &t;
            }
            if (wet != nullptr)
                add (aLowEnd, "No lows in the reverbs", true, 0.4f, trackLabel (*wet) + " carries " + db (wet->lowMidDb - wet->outRmsDb) + " under 150 Hz",
                     "the reverb's low cut up to 200 Hz (one click)", -1, false, suite::Fix::ReverbLowCut, 0, true, feature (AP::reverbLowsF));
            // 44. a section leaning left or right
            float lean = 0.0f; int leanBar = -1;
            for (const auto& s : S)
            {
                if (! s.heard()) continue;
                std::vector<float> l;
                for (int b = s.startBar - 1; b < s.startBar - 1 + s.bars && b < (int) in.bars.size(); ++b) if (in.bars[(size_t) b].heard) l.push_back (in.bars[(size_t) b].leanDb);
                if (l.empty()) continue;
                float m = 0.0f; for (auto v : l) m += v; m /= (float) l.size();
                if (std::abs (m) > std::abs (lean)) { lean = m; leanBar = s.startBar; }
            }
            if (leanBar > 0)
                add (aTranslation, "Balanced left and right", true, ramp (std::abs (lean), 1.0f, 3.0f), "leans " + juce::String (std::abs (lean), 1) + " dB to the " + (lean > 0 ? "left" : "right") + " at bar " + juce::String (leanBar),
                     "the leaning track re-centred (the autopilot)", leanBar, false, suite::Fix::None, 0, false, feature (AP::balanceF));
            // 45. width rising into the drop
            for (auto bi : builds)
            {
                int next = -1; for (size_t j = (size_t) bi + 1; j < S.size() && next < 0; ++j) if (S[j].type == T::Drop) next = (int) j;
                if (next < 0 || ! S[(size_t) bi].heard() || ! S[(size_t) next].heard()) continue;
                const auto rise = S[(size_t) next].width() - S[(size_t) bi].width();
                add (aMids, "Width opens into the drop", true, ramp (rise, 1.0f, -2.0f), "the drop is " + db (rise) + " wider than its build",
                     "the build narrows and the drop opens (the director: wider drop)", S[(size_t) next].startBar, false, suite::Fix::None, 0, false, feature (AP::widthBuildF));
                break;
            }
            // 46. doubles that vanish in mono
            const suite::TrackTraits::Traits* sideHeavy = nullptr;
            for (const auto& t : in.traits) if (heard (t) && t.sideDb > t.outRmsDb && (sideHeavy == nullptr || t.sideDb - t.outRmsDb > sideHeavy->sideDb - sideHeavy->outRmsDb)) sideHeavy = &t;
            add (aTranslation, "Doubles survive mono", ! in.traits.empty(), sideHeavy == nullptr ? 1.0f : 0.4f,
                 sideHeavy == nullptr ? juce::String ("no part is more side than middle") : trackLabel (*sideHeavy) + " is more side than middle: it thins out in mono",
                 "that part a little narrower (one click)", -1, false, sideHeavy != nullptr ? suite::Fix::Narrower : suite::Fix::None, sideHeavy != nullptr ? (sideHeavy->id & 0xfffu) : 0u, true,
                 feature (AP::monoDoublesF));
        }

        // ===================== K. top end =====================
        if (const auto* h = loudestOf (3))
        {
            Bands hb {};
            if (bandsOf (*h, hb))
            {
                const auto air = powerSumDb (hb, 8, 9) - hb[6];
                add (aMids, "Hats against the label", true, juce::jmin (ramp (air, -4.0f, -12.0f), ramp (air, 6.0f, 12.0f)), trackLabel (*h) + ": 8-16 kHz " + db (air) + " against 2 kHz",
                     air < -4.0f ? juce::String ("brighter hats (one click)") : juce::String ("less fizz on the hats (one click)"), -1, false,
                     suite::Fix::MasterTone, suite::packMasterTone (h->id, 8, air < -4.0f ? 1.0f : -1.0f), true, feature (AP::hatsToneF));
                const auto bite = hb[7] - (hb[6] + hb[8]) * 0.5f;
                add (aMids, "Attacks that bite", true, ramp (bite, 3.0f, 8.0f), trackLabel (*h) + ": 4 kHz sticks out " + db (bite),
                     "a softer 4 kHz on the hits (one click)", -1, false, suite::Fix::MasterTone, suite::packMasterTone (h->id, 7, -1.0f), true, feature (AP::harshAttackF));
            }
        }
        if (const auto* fx = loudestOf (7))
            if (const auto* l = loudestOf (5))
            {
                Bands fb {}, lb {};
                if (bandsOf (*fx, fb) && bandsOf (*l, lb))
                {
                    const auto d = powerSumDb (fb, 8, 9) - powerSumDb (lb, 8, 9);
                    add (aMids, "Noise FX under the lead", true, ramp (d, -3.0f, 3.0f), trackLabel (*fx) + "'s top " + db (d) + " against the lead's",
                         "the FX tops down under the lead (one click, or the autopilot)", -1, false, suite::Fix::MasterTone, suite::packMasterTone (fx->id, 8, -1.0f), true, feature (AP::fxUnderLeadF));
                }
            }

        // ===================== M. groove =====================
        if (in.swingMs.size() >= 2)
        {
            float lo = 1000.0f, hi = -1000.0f;
            for (const auto& [role, ms] : in.swingMs) { lo = juce::jmin (lo, ms); hi = juce::jmax (hi, ms); }
            add (aHygiene, "One swing", true, ramp (hi - lo, 6.0f, 15.0f), "the parts' offbeats sit " + juce::String (juce::roundToInt (hi - lo)) + " ms apart",
                 "every part on one swing (Correspondence's exports quantised to the groove)", -1, false, suite::Fix::None, 0, false, feature (AP::tightSwingF));
        }
        if (in.flamMs >= 0.0f)
            add (aLowEnd, "Kick and bass start together", true, ramp (in.flamMs, 4.0f, 12.0f), "the bass starts " + juce::String (in.flamMs, 1) + " ms from the kick on shared beats",
                 "the layers lined up (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::layerAlignF));
        if (in.song.valid && in.song.bass.size() >= 16)
        {
            float onAcc = 0.0f, offAcc = 0.0f; int nOn = 0, nOff = 0, ghosts = 0; float ghostVel = 0.0f;
            for (const auto& n : in.song.bass)
            {
                const auto frac = n.start - std::floor (n.start);
                if (std::abs (frac - 0.5f) < 0.02f) { onAcc += (float) n.velocity; ++nOn; } else { offAcc += (float) n.velocity; ++nOff; }
                if (n.velocity < 45) { ++ghosts; ghostVel += (float) n.velocity; }
            }
            if (nOn > 0 && nOff > 0)
            {
                const auto contrast = onAcc / (float) nOn - offAcc / (float) nOff;
                add (aWriting, "Accents line up", true, ramp (contrast, 8.0f, 0.0f), "the offbeat 8ths " + juce::String (juce::roundToInt (contrast)) + " velocity over the other notes",
                     "the offbeats accented on the bass and arp, with the drums (Correspondence, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::accentsF));
            }
            if (ghosts > 0)
                add (aWriting, "Ghost notes heard", true, ramp (ghostVel / (float) ghosts, 35.0f, 15.0f), juce::String (ghosts) + " ghost note(s) at velocity " + juce::String (juce::roundToInt (ghostVel / (float) ghosts)),
                     "the ghost notes a little louder (Correspondence, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::ghostLiftF));
        }

        // ===================== N. harmony =====================
        if (in.song.valid && in.song.bass.size() >= 8 && in.song.quartersPerChord > 0.0)
        {
            int thirds = 0, strong = 0;
            for (const auto& n : in.song.bass)
            {
                if (std::abs (n.start - std::round (n.start)) > 0.02f) continue;
                const auto sec = sectionAtBar ((int) std::floor (n.start / in.barQ) + 1);
                if (sec < 0) continue;
                const auto t = (size_t) in.map.sections[(size_t) sec].type;
                const auto cnt = in.song.chordCount[t];
                if (cnt <= 0) continue;
                const auto slot = (int) std::floor ((n.start - (in.map.sections[(size_t) sec].startBar - 1) * in.barQ) / in.song.quartersPerChord) % cnt;
                const auto rel = ((n.pitch - in.song.root - in.song.chordRoots[t][(size_t) slot]) % 12 + 12) % 12;
                ++strong; thirds += rel == 3 || rel == 4 ? 1 : 0;
            }
            if (strong >= 8)
                add (aWriting, "The bass on the roots", true, ramp ((float) thirds / (float) strong, 0.1f, 0.35f), juce::String (thirds) + " of " + juce::String (strong) + " beats put the bass on the chord's third",
                     "the bass to the root there (Correspondence, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::bassRootsF));
        }
        // 64. tension into each drop and release in it (how far each bar sits from a plain triad)
        for (auto d : drops)
        {
            const auto start = S[(size_t) d].startBar - 1;
            if (start < 4 || start + 4 > (int) in.bars.size()) continue;
            float before = 0.0f, after = 0.0f, sb = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                barRoot (in.bars[(size_t) (start - 4 + k)], &sb); before += 1.0f - sb;
                barRoot (in.bars[(size_t) (start + k)], &sb); after += 1.0f - sb;
            }
            add (aWriting, "Tension and release", true, ramp ((before - after) / 4.0f, 0.1f, -0.05f), "the harmony is " + juce::String (juce::roundToInt ((before - after) / 4.0f * 100.0f)) + "% more unsettled before the drop than in it",
                 "a sus or a V chord at the end of the build (Correspondence: harmony, the autopilot)", start + 1, false, suite::Fix::None, 0, false, feature (AP::dropTensionF));
            break;
        }
        // 65. the melody's peak climbs through the song
        if (in.song.valid && in.song.dropCount >= 2)
        {
            const auto first = in.song.leadPeakPerDrop[0], last = in.song.leadPeakPerDrop[(size_t) juce::jmin (3, in.song.dropCount - 1)];
            if (first > 0 && last > 0)
                add (aWriting, "The melody climbs", true, last > first ? 1.0f : last == first ? 0.6f : 0.3f, "the lead's top note: MIDI " + juce::String (first) + " in the first drop, " + juce::String (last) + " in the last",
                     "the lead an octave up in the later drops (Correspondence, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::melodyClimbF));
        }
        // 66. notes held across a chord change (the old root still ringing after the change)
        if (in.song.valid && songHeard && in.song.quartersPerChord > 0.0)
        {
            int changes = 0, hang = 0, hangBar = -1;
            for (int i = 0; i < in.map.count; ++i)
            {
                const auto t = (size_t) in.map.sections[(size_t) i].type;
                const auto cnt = in.song.chordCount[t];
                if (cnt < 2) continue;
                for (int c = 1; c < cnt; ++c)
                {
                    const auto q = (in.map.sections[(size_t) i].startBar - 1) * in.barQ + c * in.song.quartersPerChord;
                    const auto bar = (int) std::floor (q / in.barQ);
                    if (bar <= 0 || bar >= (int) in.bars.size() || bar >= in.map.endBar (i) - 1) continue;
                    const auto oldPc = (in.song.root + in.song.chordRoots[t][(size_t) c - 1] + 120) % 12, newPc = (in.song.root + in.song.chordRoots[t][(size_t) c] + 120) % 12;
                    if (oldPc == newPc) continue;
                    const auto& ch = in.bars[(size_t) bar].chroma;
                    ++changes;
                    const auto inNew = ((oldPc - newPc + 12) % 12) == 0 || ((oldPc - newPc + 12) % 12) == 3 || ((oldPc - newPc + 12) % 12) == 4 || ((oldPc - newPc + 12) % 12) == 7;
                    if (! inNew && ch[(size_t) oldPc] > 0.2f) { ++hang; if (hangBar < 0) hangBar = bar + 1; }
                }
            }
            if (changes >= 4)
                add (aWriting, "Clean chord changes", true, ramp ((float) hang / (float) changes, 0.1f, 0.4f), juce::String (hang) + " of " + juce::String (changes) + " changes keep the old chord ringing",
                     "notes trimmed at the change (the autopilot: hangovers)", hangBar, false, suite::Fix::None, 0, false, feature (AP::hangoverTrimF));
        }
        // 31. a hook you remember: the first two bars come back in the next two
        if (in.song.valid && in.song.hook.size() >= 6)
        {
            std::vector<std::pair<int, int>> a, b;   // (16th, pitch) in bars 1-2 and 3-4
            for (const auto& n : in.song.hook)
            {
                const auto rel = n.start - (float) in.song.hookStartQ;
                const auto step = (int) std::lround (std::fmod (rel, 8.0f) * 4.0f);
                if (rel < 8.0f) a.push_back ({ step, n.pitch }); else if (rel < 16.0f) b.push_back ({ step, n.pitch });
            }
            int same = 0; for (const auto& x : a) same += std::find (b.begin(), b.end(), x) != b.end() ? 1 : 0;
            const auto rep = a.empty() ? 0.0f : (float) same / (float) a.size();
            add (aWriting, "A hook you remember", ! a.empty() && ! b.empty(), juce::jmin (ramp (rep, 0.5f, 0.15f), ramp (rep, 0.95f, 1.01f) + 0.5f),
                 juce::String (juce::roundToInt (rep * 100.0f)) + "% of its first two bars come back in the next two", "say it twice: the opening cell again, the ending changed (Correspondence: call and response, or Write the hook stronger)",
                 -1, false, suite::Fix::None, 0, false, feature (AP::callResponseF));
        }
        // 32. chord voicings crowding the lead's register
        if (in.song.valid && in.song.chordHigh > 0 && ! in.song.hook.empty())
        {
            int lo = 127; for (const auto& n : in.song.hook) lo = juce::jmin (lo, n.pitch);
            const auto overlap = in.song.chordHigh - lo;
            add (aWriting, "Chords leave the lead room", true, ramp ((float) overlap, 0.0f, 7.0f), "the chords reach " + juce::String (juce::jmax (0, overlap)) + " semitones into the lead's range",
                 "the chords' top notes an octave down under the lead (Correspondence, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::voicingRoomF));
        }
        // 33. the groove differs between sections
        {
            const SectionStat* groove = nullptr; const SectionStat* drop = nullptr;
            for (const auto& s : S) { if (s.heard() && (s.type == T::Groove || s.type == T::Intro) && groove == nullptr) groove = &s; if (s.heard() && s.type == T::Drop && drop == nullptr) drop = &s; }
            if (groove != nullptr && drop != nullptr)
            {
                const auto diff = std::abs (drop->onsetsPerBar - groove->onsetsPerBar) / juce::jmax (1.0f, groove->onsetsPerBar);
                add (aWriting, "The drop has its own groove", true, ramp (diff, 0.25f, 0.05f), "the drop has " + juce::String (juce::roundToInt (diff * 100.0f)) + "% more (or fewer) events per bar than the groove",
                     "a different chord rhythm per section: held in the breakdowns, gated in the drops (Correspondence, the autopilot)", drop->startBar, false, suite::Fix::None, 0, false, feature (AP::chordRhythmF));
            }
        }
        // 34. the motif comes back changed
        if (in.song.valid && ! in.song.breakdownLead.empty() && in.song.hook.size() >= 4)
        {
            const auto sim = printSimilarity (hookPrint (in.song.hook), hookPrint (in.song.breakdownLead));
            add (aWriting, "The motif develops", true, juce::jmin (ramp (sim, 0.3f, 0.0f), ramp (sim, 0.9f, 1.01f) + 0.4f), "the breakdown's lead is " + juce::String (juce::roundToInt (sim * 100.0f)) + "% like the hook",
                 sim < 0.3f ? juce::String ("let the breakdown hint at the hook (Correspondence: write for the label)") : juce::String ("change it a little in the breakdown: slower, fewer notes, another register"));
        }
        // 35. the key and tempo against the label's catalogue
        if (! P.refs.empty())
        {
            std::vector<std::pair<float, float>> bpms;
            std::map<int, int> keys;
            for (const auto& r : P.refs) { bpms.push_back ({ (float) r.bpm, 1.0f }); if (r.keyRoot >= 0) ++keys[r.keyRoot]; }
            const auto st = statOf (bpms);
            juce::String keyText;
            if (! keys.empty() && in.song.valid)
            {
                static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
                const auto minorRoot = (in.song.scale == 1 || in.song.scale == 5) ? (in.song.root + 9) % 12 : (in.song.root % 12 + 12) % 12;   // keys are kept as their minor
                keyText = ", the key (" + juce::String (names[(in.song.root % 12 + 12) % 12]) + ", or its relative) used by " + juce::String (keys.count (minorRoot) ? keys[minorRoot] : 0)
                          + " of the label's " + juce::String ((int) P.refs.size()) + " tracks";
            }
            add (aReference, "Tempo and key", true, fromOutside (st.outside ((float) in.bpm)), juce::String (in.bpm, 1) + " BPM (the label: " + st.text (0) + ")" + keyText,
                 "the tempo the label plays its tracks at (a different key is fine)");
        }

        // ===================== O, H. production and render safety =====================
        {
            int idle = 0; juce::StringArray names;
            for (const auto& e : in.entries) if (e.has (suite::Status::idle)) { ++idle; if (names.size() < 4) names.add (e.plugin + (e.track.isNotEmpty() ? " (" + e.track + ")" : juce::String())); }
            add (aHygiene, "Nothing loaded doing nothing", ! in.entries.empty(), ramp ((float) idle, 0.0f, 4.0f), idle == 0 ? juce::String ("every suite plugin is doing something") : juce::String (idle) + " idle: " + names.joinIntoString (", "),
                 "idle plugins pass sound through without working (the autopilot), or remove them", -1, false, suite::Fix::None, 0, false, feature (AP::idleSleepF));
            const suite::TrackTraits::Traits* stage = nullptr; float off = 0.0f;
            for (const auto& t : in.traits) if (t.inRmsDb > -90.0f && std::abs (t.inRmsDb + 18.0f) > off) { off = std::abs (t.inRmsDb + 18.0f); stage = &t; }
            if (stage != nullptr)
                add (aHygiene, "Gain staging into the plugins", true, ramp (off, 8.0f, 16.0f), trackLabel (*stage) + " comes in at " + db (stage->inRmsDb) + " RMS (aim: about -18)",
                     "each Quintessence stages its own input and gives the level back after (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::inputStageF));
            // 35/H. the render against playback
            const suite::TrackTraits::Traits* differs = nullptr; float by = 0.0f; int at = -1;
            bool rendered = false;
            for (const auto& t : in.traits)
                for (int i = 0; i < suite::TrackTraits::maxSections; ++i)
                    if (t.sectionDbOffline[(size_t) i] > -90.0f && t.sectionDb[(size_t) i] > -90.0f)
                    {
                        rendered = true;
                        const auto d = t.sectionDbOffline[(size_t) i] - t.sectionDb[(size_t) i];
                        if (std::abs (d) > std::abs (by)) { by = d; differs = &t; at = i; }
                    }
            add (aHygiene, "The render sounds like playback", rendered, ramp (std::abs (by), 0.5f, 2.0f),
                 differs == nullptr ? juce::String ("the render matches playback") : trackLabel (*differs) + " is " + db (by) + " in the render" + (at >= 0 && at < in.map.count ? " at bar " + juce::String (in.map.sections[(size_t) at].startBar) : juce::String()),
                 "a plugin on that track renders differently offline: check its quality or oversampling settings for rendering", at >= 0 && at < in.map.count ? in.map.sections[(size_t) at].startBar : -1);
        }
    }
}
