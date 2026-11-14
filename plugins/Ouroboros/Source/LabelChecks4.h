#pragma once

// The fourth set of label-ready checks, each naming the autopilot switch that fixes it: each
// role's tone where it's most exposed, the loudness journey and the width section by section,
// the dynamics phrase by phrase, kick tails and sub hand-overs, sub phase per bar, cumulative
// harshness, the air, risers against the drop, tails before the drop, section openings and
// endings, the breakdown's bed and swell, the outro's groove, parts that never move, parts that
// don't cut through, drum transients against each other, the sub under the bass, the pads behind
// the lead, space per section, subsonics, the suite's CPU, and a few checks with no automatic fix
// (transition lengths, one-off sounds, a second breakdown, bus structure). Included at the end of
// LabelReady.h; evaluate() calls checks4().

namespace lab
{
    inline void checks4 (const Inputs& in, const Profile& P, std::vector<Check>& out)
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
        std::vector<int> drops, breaks, builds;
        for (const auto& s : S) { if (s.type == T::Drop) drops.push_back (s.index); if (s.type == T::Breakdown) breaks.push_back (s.index); if (s.type == T::Build) builds.push_back (s.index); }
        int heardBars = 0; for (const auto& b : in.bars) heardBars += b.heard ? 1 : 0;
        const auto songHeard = heardBars >= 16;

        // ===== each role's tone, where it's most exposed =====
        {
            static const int mainBand[8] = { -1, 1, 1, 8, 4, 6, 6, 8 };
            const suite::TrackTraits::Traits* worstT = nullptr; float worstD = 0.0f; int worstBand = 0; int worstType = 0;
            for (const auto& t : in.traits)
            {
                if (t.role < 1 || t.role > 7 || ! heard (t)) continue;
                // the section where it sits highest against the mix
                int bestI = -1; float bestM = -200.0f;
                for (const auto& s : S)
                    if (s.heard() && s.index < suite::TrackTraits::maxSections && t.sectionDb[(size_t) s.index] > -60.0f && t.sectionDb[(size_t) s.index] - s.db > bestM)
                    { bestM = t.sectionDb[(size_t) s.index] - s.db; bestI = s.index; }
                if (bestI < 0) continue;
                const auto b = mainBand[t.role];
                const auto sh = shape (S[(size_t) bestI].band);
                const auto& st = P.tone[(size_t) S[(size_t) bestI].type][(size_t) b];
                if (! st.valid()) continue;
                const auto d = sh[(size_t) b] - st.median;
                if (std::abs (d) > std::abs (worstD) && st.outside (sh[(size_t) b]) > 0.0f) { worstD = d; worstT = &t; worstBand = b; worstType = (int) S[(size_t) bestI].type; }
            }
            if (worstT != nullptr)
                add (aMids, "Each part's tone where it's exposed", true, ramp (std::abs (worstD), 2.0f, 6.0f),
                     trackLabel (*worstT) + " in the " + juce::String (sectionName ((T) worstType)).toLowerCase() + ": " + juce::String (juce::roundToInt (analysis::bandCentres()[(size_t) worstBand])) + " Hz " + db (worstD) + " against the label",
                     "a step toward the label on that track (one click, or the autopilot)", -1, false, suite::Fix::MasterTone,
                     suite::packMasterTone (worstT->id, worstBand, worstD > 0.0f ? -1.0f : 1.0f), true, feature (AP::roleTimbreF));
            else if (! in.traits.empty() && songHeard) add (aMids, "Each part's tone where it's exposed", true, 1.0f, "every part sits within the label's range where it's most exposed");
        }
        // ===== the loudness journey and the width, section by section =====
        if (songHeard && ! drops.empty() && S[(size_t) drops[0]].heard())
        {
            const auto dropDb = S[(size_t) drops[0]].db;
            float worst = 0.0f; int worstType = -1, worstBar = -1; float worstW = 0.0f; int worstWType = -1, worstWBar = -1;
            for (const auto& s : S)
            {
                if (! s.heard() || s.type == T::Drop) continue;
                std::vector<std::pair<float, float>> rel;
                for (const auto& r : P.refs) if (r.has[(size_t) s.type] && r.has[(size_t) T::Drop]) rel.push_back ({ r.level[(size_t) s.type] - r.level[(size_t) T::Drop], Profile::weightOf (r) });
                if (rel.size() >= 1)
                {
                    const auto target = statOf (rel).median;
                    const auto d = (s.db - dropDb) - target;
                    if (std::abs (d) > std::abs (worst)) { worst = d; worstType = (int) s.type; worstBar = s.startBar; }
                }
                const auto& w = P.width[(size_t) s.type];
                if (w.valid() && w.outside (s.width()) > 0.0f)
                {
                    const auto dw = s.width() - w.median;
                    if (std::abs (dw) > std::abs (worstW)) { worstW = dw; worstWType = (int) s.type; worstWBar = s.startBar; }
                }
            }
            if (worstType >= 0)
                add (aReference, "The loudness journey", true, ramp (std::abs (worst), 1.5f, 5.0f),
                     "the " + juce::String (sectionName ((T) worstType)).toLowerCase() + "s sit " + db (worst) + " off the label's level against the drop",
                     "every track a step in those sections (one click, or the autopilot)", worstBar, false, suite::Fix::SectionTrim,
                     suite::packSectionTrim (0, worstType, worst > 0.0f ? -1.0f : 1.0f), true, feature (AP::journeyF));
            if (worstWType >= 0)
                add (aMids, "Width section by section", true, ramp (std::abs (worstW), 2.0f, 6.0f),
                     "the " + juce::String (sectionName ((T) worstWType)).toLowerCase() + "s are " + db (worstW) + " off the label's width",
                     worstW > 0.0f ? juce::String ("narrower there (the autopilot)") : juce::String ("wider there (the autopilot)"), worstWBar, false, suite::Fix::SectionWidth,
                     suite::packSectionTrim (0, worstWType, worstW > 0.0f ? -2.0f : 2.0f), true, feature (AP::sectionWidthF));
        }
        // ===== the dynamics phrase by phrase (the drops) =====
        if (songHeard)
        {
            float flattest = 100.0f; int at = -1;
            for (auto d : drops)
                for (int b = S[(size_t) d].startBar - 1; b + 8 <= S[(size_t) d].startBar - 1 + S[(size_t) d].bars && b + 8 <= (int) in.bars.size(); b += 8)
                {
                    float pk = -100.0f; std::vector<float> lv;
                    for (int k = 0; k < 8; ++k) { pk = juce::jmax (pk, in.bars[(size_t) (b + k)].peakDb); lv.push_back (in.bars[(size_t) (b + k)].db); }
                    const auto crest = pk - powerMeanDb (lv);
                    if (pk > -99.0f && crest < flattest) { flattest = crest; at = b + 1; }
                }
            if (at > 0)
                add (aHygiene, "Every phrase breathes", true, ramp (flattest, 8.0f, 5.0f), "the flattest 8 bars have " + juce::String (flattest, 1) + " dB between peaks and level",
                     "less glue and multiband there (one click, or the autopilot)", at, false, suite::Fix::LessSquash, 0, true, feature (AP::phraseDynamicsF));
        }
        // ===== low end: kick tails, the sub hand-over, sub phase, the sub under the bass =====
        if (const auto* k = loudestOf (1))
            if (k->decayCount >= 3)
            {
                const auto beats = 3.0f * k->decayMs / beatMs;
                add (aLowEnd, "Kick tail in the drops", true, ramp (beats, 0.6f, 1.0f), trackLabel (*k) + " rings " + juce::String (beats, 2) + " beats",
                     "its tail shortened in the drops, its hit kept (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::kickTailF));
            }
        for (auto bi : builds)
        {
            int next = -1; for (size_t j = (size_t) bi + 1; j < S.size() && next < 0; ++j) if (S[j].type == T::Drop) next = (int) j;
            if (next < 0 || ! S[(size_t) bi].heard() || ! S[(size_t) next].heard()) continue;
            const auto& b = S[(size_t) bi]; const auto& d = S[(size_t) next];
            const auto lowGain = (powerSumDb (d.band, 0, 1) - d.db) - (powerSumDb (b.band, 0, 1) - b.db);
            add (aLowEnd, "The sub hands over into the drop", true, ramp (lowGain, 3.0f, 0.0f), "the drop brings " + db (lowGain) + " more sub (relative) than its build",
                 "the bass's lows thin out as the build rises, back on the drop (the autopilot)", d.startBar, false, suite::Fix::None, 0, false, feature (AP::subHandoverF));
            break;
        }
        if (songHeard)
        {
            int bad = 0, n = 0, first = -1;
            for (size_t b = 0; b < in.bars.size(); ++b)
                if (in.bars[b].heard && in.bars[b].lowCorrCount > 0)
                {
                    ++n;
                    if (in.bars[b].lowCorr < 0.5f) { ++bad; if (first < 0) first = (int) b + 1; }
                }
            if (n >= 8)
                add (aLowEnd, "Sub phase bar by bar", true, ramp ((float) bad / (float) n, 0.02f, 0.15f), juce::String (bad) + " of " + juce::String (n) + " bars cancel under 120 Hz (first at bar " + juce::String (first) + ")",
                     "the polarity flipped where the kick and bass cancel (Kybalion, one click)", first, false, suite::Fix::PhaseFlip, 0, true, feature (AP::subPhaseF));
        }
        if (const auto* b = loudestOf (2))
        {
            Bands bb {};
            if (bandsOf (*b, bb))
            {
                const auto sub = bb[0] - bb[1];
                add (aLowEnd, "Sub under the bass", true, ramp (sub, -10.0f, -18.0f), trackLabel (*b) + ": 31 Hz " + db (sub) + " against 63 Hz",
                     "a little more sub on the bass (one click, or the autopilot)", -1, false, suite::Fix::MasterTone, suite::packMasterTone (b->id, 0, 1.0f), true, feature (AP::subFillF));
            }
        }
        {
            const suite::TrackTraits::Traits* sub = nullptr;
            for (const auto& t : in.traits) if (t.subsonicDb > -100.0f && (sub == nullptr || t.subsonicDb > sub->subsonicDb)) sub = &t;
            if (sub != nullptr)
                add (aHygiene, "No subsonics", true, ramp (sub->subsonicDb, -40.0f, -20.0f), trackLabel (*sub) + ": under 20 Hz " + db (sub->subsonicDb) + " against its level",
                     "a subsonic filter on that track (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::subsonicF));
        }

        // ===== top end: cumulative harshness, air, the riser against the drop, tails before the drop =====
        if (songHeard && ! drops.empty())
        {
            double expo = 0.0; int n = 0;
            for (auto d : drops)
                for (int b = S[(size_t) d].startBar - 1; b < S[(size_t) d].startBar - 1 + S[(size_t) d].bars && b < (int) in.bars.size(); ++b)
                    if (in.bars[(size_t) b].heard) { const auto sh = shape (in.bars[(size_t) b].band); expo += 0.5 * (sh[6] + sh[7]); ++n; }
            if (n > 0)
            {
                const auto mean = (float) (expo / n), lab = 0.5f * (P.toneOf (T::Drop)[6] + P.toneOf (T::Drop)[7]);
                const suite::TrackTraits::Traits* bright = nullptr; float bb = -200.0f; Bands b {};
                for (const auto& t : in.traits) if (bandsOf (t, b) && b[7] > bb) { bb = b[7]; bright = &t; }
                add (aMids, "Harshness over the whole song", true, ramp (mean - lab, 1.5f, 4.0f), "2-5 kHz across " + juce::String (n) + " drop bars " + db (mean - lab) + " against the label",
                     "a little less 4 kHz on the brightest part (one click, or the autopilot)", -1, false, bright != nullptr ? suite::Fix::MasterTone : suite::Fix::None,
                     bright != nullptr ? suite::packMasterTone (bright->id, 7, -1.0f) : 0u, true, feature (AP::harshExposureF));
                const auto air = 0.0f;   // (the shape's top band against the label's, below)
                juce::ignoreUnused (air);
            }
            const auto& d = S[(size_t) drops[0]];
            if (d.heard())
            {
                const auto top = shape (d.band)[9] - P.toneOf (T::Drop)[9];
                const suite::TrackTraits::Traits* h = loudestOf (3);
                add (aMids, "Air on top", true, ramp (std::abs (top), 2.5f, 6.0f), "16 kHz " + db (top) + " against the label's drops",
                     top < 0.0f ? juce::String ("more air on the hats (one click, or the autopilot)") : juce::String ("less fizz on the hats (one click, or the autopilot)"), d.startBar, false,
                     h != nullptr ? suite::Fix::MasterTone : suite::Fix::None, h != nullptr ? suite::packMasterTone (h->id, 9, top < 0.0f ? 1.0f : -1.0f) : 0u, true, feature (AP::airF));
                const auto start = d.startBar - 1;
                if (start >= 2 && start < (int) in.bars.size())
                {
                    const auto riser = powerSumDb (in.bars[(size_t) start - 1].band, 8, 9), drop = powerSumDb (in.bars[(size_t) start].band, 8, 9);
                    const suite::TrackTraits::Traits* fx = loudestOf (7);
                    add (aAR, "The riser peaks under the drop", true, ramp (riser - drop, 0.0f, 4.0f), "the last build bar's top end " + db (riser - drop) + " against the drop's first bar",
                         "the riser's top a little lower so the drop is the brightest moment (one click, or the autopilot)", d.startBar, false,
                         fx != nullptr ? suite::Fix::MasterTone : suite::Fix::None, fx != nullptr ? suite::packMasterTone (fx->id, 8, -1.0f) : 0u, true, feature (AP::riserTopF));
                }
            }
        }
        {
            const suite::TrackTraits::Traits* tailer = nullptr;
            for (const auto& t : in.traits) if ((t.role == 3 || t.role == 7) && t.decayCount >= 3 && 3.0f * t.decayMs > beatMs && (tailer == nullptr || t.decayMs > tailer->decayMs)) tailer = &t;
            add (aAR, "Tails cleared before the drop", ! in.traits.empty(), tailer == nullptr ? 1.0f : 0.5f,
                 tailer == nullptr ? juce::String ("no hats or FX ring past a beat") : trackLabel (*tailer) + " rings " + juce::String (3.0f * tailer->decayMs / beatMs, 1) + " beats into whatever follows",
                 "hats and FX cut for the last 16th before each drop (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::chokeF));
        }

        // ===== arrangement: section openings and endings, the breakdown's bed and swell, the outro's groove =====
        if (songHeard && in.map.count > 2)
        {
            int flatOpen = 0, flatEnd = 0, n = 0, firstBar = -1;
            for (int i = 1; i < in.map.count; ++i)
            {
                const auto start = in.map.sections[(size_t) i].startBar - 1;
                if (start < 2 || start >= (int) in.bars.size()) continue;
                const auto& a = in.bars[(size_t) start - 1]; const auto& b = in.bars[(size_t) start]; const auto& c = in.bars[(size_t) start - 2];
                if (! a.heard || ! b.heard || ! c.heard) continue;
                ++n;
                const auto opens = std::abs (b.db - a.db) > 1.5f || distance (a.band, b.band) > 1.5f || std::abs (b.onsets - a.onsets) > 2.0f;
                const auto setsUp = std::abs (a.db - c.db) > 1.0f || std::abs (powerSumDb (a.band, 7, numBands - 1) - powerSumDb (c.band, 7, numBands - 1)) > 1.5f;
                flatOpen += opens ? 0 : 1; flatEnd += setsUp ? 0 : 1;
                if ((! opens || ! setsUp) && firstBar < 0) firstBar = start + 1;
            }
            if (n > 0)
                add (aAR, "Section openings and endings", true, ramp ((float) (flatOpen + flatEnd) / (float) (2 * n), 0.1f, 0.5f),
                     juce::String (flatOpen) + " section(s) open without a change, " + juce::String (flatEnd) + " end without setting up the next",
                     "an impact or a new layer at each opening, a riser peak or a fill at each ending (the director, the autopilot)", firstBar, false,
                     suite::Fix::None, 0, false, feature (AP::sectionEdgesF));
        }
        for (auto bi : breaks)
        {
            const auto& b = S[(size_t) bi];
            if (! b.heard()) continue;
            int parts = 0;
            for (const auto& t : in.traits) if (bi < suite::TrackTraits::maxSections && t.sectionDb[(size_t) bi] > -45.0f) ++parts;
            float strength = 0.0f; int n = 0;
            for (int k = b.startBar - 1; k < b.startBar - 1 + b.bars && k < (int) in.bars.size(); ++k) { float sc = 0.0f; if (barRoot (in.bars[(size_t) k], &sc) >= 0) { strength += sc; ++n; } }
            const auto chords = n > 0 ? strength / (float) n : 0.0f;
            add (aAR, "The breakdown has a bed", ! in.traits.empty() || n > 0, juce::jmin (ramp ((float) parts, 3.0f, 1.0f), ramp (chords, 0.5f, 0.2f)),
                 juce::String (parts) + " part(s) play in it; its chords " + (chords > 0.45f ? "clear" : "faint"),
                 "a piano or string bed holding the chords, the hook hinted over it (Correspondence, the autopilot)", b.startBar, false, suite::Fix::None, 0, false, feature (AP::breakdownBedF));
            if (b.bars >= 24)
            {
                float moves = 0.0f; int m = 0;
                for (int k = b.startBar; k < b.startBar - 1 + b.bars && k < (int) in.bars.size(); ++k)
                    if (in.bars[(size_t) k].heard && in.bars[(size_t) k - 1].heard) { moves += std::abs (in.bars[(size_t) k].db - in.bars[(size_t) k - 1].db) + 0.3f * std::abs (in.bars[(size_t) k].onsets - in.bars[(size_t) k - 1].onsets); ++m; }
                add (aAR, "A long breakdown keeps moving", m > 0, ramp (m > 0 ? moves / (float) m : 0.0f, 0.8f, 0.2f), juce::String (b.bars) + " bars; it changes " + juce::String (m > 0 ? moves / (float) m : 0.0f, 2) + " dB a bar",
                     "a swell, a new layer or a chord change halfway (the director, the autopilot)", b.startBar, false, suite::Fix::None, 0, false, feature (AP::breakdownSwellF));
            }
            break;
        }
        if (! S.empty() && S.back().type == T::Outro && S.back().heard() && ! drops.empty() && S[(size_t) drops.back()].heard())
        {
            const auto& o = S.back();
            const auto groove = o.onsetsPerBar / juce::jmax (0.5f, S[(size_t) drops.back()].onsetsPerBar);
            const auto lows = (powerSumDb (o.band, 0, 1) - o.db) - (powerSumDb (S[(size_t) drops.back()].band, 0, 1) - S[(size_t) drops.back()].db);
            add (aAR, "The outro keeps the groove", true, juce::jmin (ramp (groove, 0.5f, 0.2f), ramp (lows, -4.0f, -12.0f)),
                 juce::String (juce::roundToInt (groove * 100.0f)) + "% of the drop's events, its lows " + db (lows) + " against the drop's",
                 "the kick and bass carry on into the outro while the rest leaves (Correspondence, the autopilot)", o.startBar, false, suite::Fix::None, 0, false, feature (AP::outroGrooveF));
        }

        // ===== mix: parts that never move, parts that don't cut through, drum transients, the pads behind, space per section =====
        {
            const suite::TrackTraits::Traits* still = nullptr;
            for (const auto& t : in.traits) if ((t.role == 3 || t.role == 5 || t.role == 7) && heard (t) && t.movementCount >= 20 && (still == nullptr || t.movementDb < still->movementDb)) still = &t;
            if (still != nullptr)
                add (aMids, "Parts that move", true, ramp (still->movementDb, 1.0f, 0.3f), trackLabel (*still) + " changes " + juce::String (still->movementDb, 1) + " dB per tenth of a second",
                     "slow movement over the phrase on it, and macro rides in Correspondence's exports (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::motionF));
        }
        if (const auto* l = loudestOf (5))
        {
            Bands lb {};
            if (bandsOf (*l, lb))
            {
                const auto edge = powerSumDb (lb, 6, 8) - powerSumDb (lb, 3, 5);
                add (aMids, "The lead cuts through on its own", true, juce::jmin (ramp (edge, -6.0f, -14.0f), ramp (l->crestDb, 16.0f, 22.0f)),
                     trackLabel (*l) + ": its edge " + db (edge) + " against its body, crest " + juce::String (l->crestDb, 1) + " dB",
                     "gentle drive on the lead for harmonics (the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::driveF));
            }
        }
        {
            const auto* k = loudestOf (1); const auto* h = loudestOf (3);
            if (k != nullptr && h != nullptr)
            {
                const auto d = h->crestDb - k->crestDb;
                add (aMids, "Drum attacks against each other", true, ramp (std::abs (d), 4.0f, 9.0f), "the hats' attack " + db (d) + " against the kick's",
                     d > 0.0f ? juce::String ("a softer attack on the hats (one click)") : juce::String ("a sharper attack on the hats (one click)"), -1, false,
                     suite::Fix::TrackPunch, suite::packMasterTone (h->id, 0, d > 0.0f ? -1.0f : 1.0f), true, feature (AP::clapTransientF));
            }
        }
        {
            const suite::TrackTraits::Traits* brightPad = nullptr; float top = -200.0f; Bands pb {};
            for (const auto& t : in.traits) if (t.role == 4 && bandsOf (t, pb) && pb[8] > top) { top = pb[8]; brightPad = &t; }
            const auto* l = loudestOf (5);
            Bands lb {};
            if (brightPad != nullptr && l != nullptr && bandsOf (*l, lb))
            {
                const auto d = top - lb[8];
                add (aMids, "The pads sit behind", true, ramp (d, -4.0f, 2.0f), trackLabel (*brightPad) + "'s top " + db (d) + " against the lead's",
                     "the pad a little darker so it sits back (one click, or the autopilot)", -1, false, suite::Fix::MasterTone,
                     suite::packMasterTone (brightPad->id, 8, -1.0f), true, feature (AP::depthF));
            }
        }
        {
            float longest = 0.0f;
            for (const auto& t : in.traits) if ((t.role == 4 || t.role == 5 || t.role == 6) && t.decayCount >= 3) longest = juce::jmax (longest, 3.0f * t.decayMs / beatMs);
            if (longest > 0.0f)
                add (aMids, "Space per section", true, ramp (longest, 2.0f, 4.0f), "the longest tail rings " + juce::String (longest, 1) + " beats, the same in every section",
                     "drier drops and more space in the breakdowns (the director, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::spaceSceneF));
        }
        // ===== each section's tone, the echoes in a busy drop, the reverb stepping back for the lead =====
        if (songHeard)
        {
            float worst = 0.0f; int worstType = -1, worstBand = -1, worstBar = -1;
            std::array<bool, numTypes> seen {};
            for (const auto& s : S)
            {
                if (! s.heard() || seen[(size_t) s.type]) continue;
                seen[(size_t) s.type] = true;
                const auto sh = shape (s.band);
                for (int k = 1; k < numBands - 1; ++k)
                {
                    const auto& st = P.tone[(size_t) s.type][(size_t) k];
                    if (! st.valid() || st.outside (sh[(size_t) k]) <= 0.0f) continue;
                    const auto d = sh[(size_t) k] - st.median;
                    if (std::abs (d) > std::abs (worst)) { worst = d; worstType = (int) s.type; worstBand = k; worstBar = s.startBar; }
                }
            }
            if (worstType >= 0)
            {
                const suite::TrackTraits::Traits* who = nullptr; float most = -200.0f; Bands b {};
                for (const auto& t : in.traits) if (bandsOf (t, b) && b[(size_t) worstBand] > most) { most = b[(size_t) worstBand]; who = &t; }
                add (aMids, "Each section's tone", true, ramp (std::abs (worst), 2.0f, 6.0f),
                     "the " + juce::String (sectionName ((T) worstType)).toLowerCase() + "s at " + juce::String (juce::roundToInt (analysis::bandCentres()[(size_t) worstBand])) + " Hz " + db (worst) + " against the label"
                         + (who != nullptr ? " (mostly " + trackLabel (*who) + ")" : juce::String()),
                     "that track a step there, in those sections only (one click, or the autopilot)", worstBar, false, who != nullptr ? suite::Fix::SectionTone : suite::Fix::None,
                     who != nullptr ? suite::packSectionTone (who->id, worstType, worstBand, worst > 0.0f ? -1.0f : 1.0f) : 0u, true, feature (AP::sectionToneF));
            }
            if (! drops.empty() && S[(size_t) drops[0]].heard())
            {
                bool antioch = false;
                for (const auto& e : in.entries) antioch = antioch || (e.plugin == "Antioch" && ! e.has (suite::Status::idle));
                const auto busy = S[(size_t) drops[0]].onsetsPerBar;
                if (antioch)
                    add (aMids, "Echoes against a busy drop", true, ramp (busy, 8.0f, 14.0f), juce::String (busy, 1) + " events a bar in the drop, with echoes on",
                         "fewer echo repeats so the drop stays clear (one click, or the autopilot)", S[(size_t) drops[0]].startBar, false, suite::Fix::EchoTighter, 0, true, feature (AP::echoTighterF));
            }
            if (const auto* l = loudestOf (5))
            {
                float tail = 0.0f;
                for (const auto& t : in.traits) if ((t.role == 4 || t.role == 7) && t.decayCount >= 3) tail = juce::jmax (tail, 3.0f * t.decayMs / beatMs);
                if (tail > 0.0f)
                    add (aMids, "The reverb steps back for the lead", true, ramp (tail, 1.5f, 3.0f), "the space rings " + juce::String (tail, 1) + " beats around " + trackLabel (*l),
                         "the reverb dips while the lead plays (one click, or the autopilot)", -1, false, suite::Fix::ReverbDuck, 0, true, feature (AP::reverbDuckF));
            }
        }
        // ===== Correspondence's parts: the groove lock =====
        if (in.song.valid && in.song.bass.size() >= 8 && in.groovePrint.length() >= 16)
        {
            bool kitOdd = false;
            for (int k = 1; k < 16; k += 2) kitOdd = kitOdd || in.groovePrint[k] != '.';
            int odd = 0;
            for (const auto& n : in.song.bass) { const auto f = n.start - std::floor (n.start); odd += std::abs (f - 0.25f) < 0.01f || std::abs (f - 0.75f) < 0.01f ? 1 : 0; }
            add (aWriting, "The bass locks with the kit", true, kitOdd ? 1.0f : ramp ((float) odd / (float) in.song.bass.size(), 0.05f, 0.3f),
                 juce::String (odd) + " bass note(s) on 16ths the kit leaves empty",
                 "those notes moved onto the kit's offbeat (Correspondence, the autopilot)", -1, false, suite::Fix::None, 0, false, feature (AP::grooveLockF));
        }
        // ===== no automatic fix: transition lengths, one-off sounds, a second breakdown, the suite's CPU =====
        if (! P.refs.empty() && ! builds.empty())
        {
            std::vector<std::pair<float, float>> lens;
            for (const auto& r : P.refs) for (const auto& s : r.sections) if (s.type == T::Build) lens.push_back ({ (float) s.bars, Profile::weightOf (r) });
            if (! lens.empty())
            {
                const auto st = statOf (lens);
                const auto& b = S[(size_t) builds[0]];
                add (aReference, "Build length", true, fromOutside (st.outside ((float) b.bars)), juce::String (b.bars) + " bars (the label: " + st.text (0) + ")",
                     "a build of about " + juce::String (juce::roundToInt (st.median)) + " bars, in FL's playlist", b.startBar);
            }
            int labelBreaks = 0, n = 0;
            for (const auto& r : P.refs) { int c = 0; for (const auto& s : r.sections) c += s.type == T::Breakdown ? 1 : 0; labelBreaks += c; ++n; }
            if (n > 0)
            {
                const auto avg = (float) labelBreaks / (float) n;
                add (aReference, "Breakdowns", true, fromOutside (std::abs ((float) breaks.size() - avg) > 0.75f ? 1.5f : 0.0f), juce::String ((int) breaks.size()) + " (the label: " + juce::String (avg, 1) + " on average)",
                     avg > (float) breaks.size() ? juce::String ("a short second breakdown, if the song can take it") : juce::String ("one breakdown is enough for this label"));
            }
        }
        if (! in.traits.empty() && in.map.count > 2)
        {
            int oneOffs = 0;
            for (const auto& t : in.traits)
            {
                int sections = 0;
                for (int i = 0; i < juce::jmin (in.map.count, suite::TrackTraits::maxSections); ++i) sections += t.sectionDb[(size_t) i] > -45.0f ? 1 : 0;
                oneOffs += sections == 1 ? 1 : 0;
            }
            add (aMids, "A consistent palette", true, ramp ((float) oneOffs, 2.0f, 5.0f), juce::String (oneOffs) + " sound(s) play in one section only",
                 "bring a few back later (changed) so the sounds develop rather than appear once");
        }
        {
            float cpu = 0.0f; int n = 0;
            for (const auto& t : in.traits) if (t.cpuPercent > 0.0f) { cpu += t.cpuPercent; ++n; }
            if (n > 0)
                add (aHygiene, "The suite's CPU", true, ramp (cpu, 40.0f, 80.0f), juce::String (n) + " Quintessence(s) use " + juce::String (cpu, 1) + "% of the audio thread's time together",
                     "freeze or render tracks you've finished (a plugin can't free CPU for you)");
        }
    }
}
