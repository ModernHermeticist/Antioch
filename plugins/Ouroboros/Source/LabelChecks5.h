#pragma once

// The fifth set of label-ready checks: the mix fixes. Each names its switches ("autopilot:a,b,c");
// the first is the one that acts on the problem it found (Ouroboros sends it, or the plugins
// latch it), the rest are the plugins' fixes for the same kind of problem, told they're needed.
// Phrase levels, the low end against the kick, the kick per section, the low end's headroom,
// parts collided in the centre, room for the lead and vocal, the drop's impact, the breakdowns
// opening up, the drums' dynamics, a clean signal, the top end, the pump against the kick, the
// space per role. Included at the end of LabelReady.h; evaluate() calls checks5().

namespace lab
{
    inline void checks5 (const Inputs& in, const Profile& P, std::vector<Check>& out)
    {
        using namespace detail;
        namespace AP = suite::Autopilot;
        auto add = [&out] (int area, const juce::String& name, bool measured, float score, const juce::String& detail, const juce::String& explain = {}, int bar = -1,
                           suite::Fix fix = suite::Fix::None, uint32_t data = 0, bool safe = false, const juce::String& action = {})
        {
            Check c;
            c.area = area; c.name = name; c.measured = measured; c.score = juce::jlimit (0.0f, 1.0f, score); c.detail = detail; c.explain = explain;
            c.bar = bar; c.fix = fix; c.fixData = data; c.safeFix = safe; c.action = action;
            out.push_back (c);
        };
        auto features = [] (std::initializer_list<AP::Feature> fs)
        {
            juce::StringArray s;
            for (auto f : fs) s.add (juce::String ((int) f));
            return "autopilot:" + s.joinIntoString (",");
        };
        const auto& S = in.sections;
        auto heard = [] (const suite::TrackTraits::Traits& t) { return t.outRmsDb > -60.0f; };
        auto firstOf = [&in, &heard] (int role) -> const suite::TrackTraits::Traits*
        {
            const suite::TrackTraits::Traits* best = nullptr;
            for (const auto& t : in.traits) if (t.role == role && heard (t) && (best == nullptr || t.outRmsDb > best->outRmsDb)) best = &t;
            return best;
        };
        int heardBars = 0; for (const auto& b : in.bars) heardBars += b.heard ? 1 : 0;
        const auto songHeard = heardBars >= 16;
        const auto haveTraits = ! in.traits.empty();
        const juce::String noTraits = "no Quintessence measuring yet";
        std::vector<int> drops, breaks, builds;
        for (const auto& s : S) { if (s.type == T::Drop) drops.push_back (s.index); if (s.type == T::Breakdown) breaks.push_back (s.index); if (s.type == T::Build) builds.push_back (s.index); }
        const auto secOf = [&S] (int index) -> const SectionStat* { for (const auto& s : S) if (s.index == index) return &s; return nullptr; };

        // ===== every phrase at its level: a 4-bar phrase sagging under the rest of its section =====
        {
            const suite::TrackTraits::Traits* worstT = nullptr; float worst = 0.0f; int worstPhrase = -1;
            const auto phraseBars = 4.0 * juce::jmax (1.0, in.barQ) / 4.0;   // (16 quarters a phrase)
            for (const auto& t : in.traits)
            {
                if (! heard (t) || t.role < 4 || t.role > 6) continue;   // pads, lead, vocal: the parts that carry a phrase
                for (const auto& s : S)
                {
                    if (! s.heard()) continue;
                    const auto p0 = (int) std::ceil ((s.startBar - 1) / phraseBars), p1 = (int) std::floor ((s.startBar - 1 + s.bars) / phraseBars) - 1;
                    std::vector<float> v;
                    for (int p = juce::jmax (0, p0); p <= juce::jmin (suite::TrackTraits::Traits::maxPhrases - 1, p1); ++p) if (t.phraseDb[(size_t) p] > -60.0f) v.push_back (t.phraseDb[(size_t) p]);
                    if (v.size() < 2) continue;
                    auto sorted = v; std::sort (sorted.begin(), sorted.end());
                    const auto median = sorted[sorted.size() / 2];
                    for (int p = juce::jmax (0, p0); p <= juce::jmin (suite::TrackTraits::Traits::maxPhrases - 1, p1); ++p)
                        if (t.phraseDb[(size_t) p] > -60.0f && median - t.phraseDb[(size_t) p] > worst) { worst = median - t.phraseDb[(size_t) p]; worstT = &t; worstPhrase = p; }
                }
            }
            if (worstT != nullptr && worst > 0.5f)
                add (aBalance, "Every phrase at its level", true, ramp (worst, 1.5f, 4.0f),
                     trackLabel (*worstT) + ": bars " + juce::String (worstPhrase * 4 + 1) + "-" + juce::String (worstPhrase * 4 + 4) + " sit " + db (-worst) + " under the rest of the section",
                     "a ride up on that phrase (one click, or the autopilot)", worstPhrase * 4 + 1, suite::Fix::PhraseTrim,
                     suite::packPhraseTrim (worstT->id, worstPhrase, juce::jmin (1.5f, 0.5f * worst)), true,
                     features ({ AP::mxPhraseTrimF, AP::mxInputLevellerF, AP::mxChainNeutralF, AP::mxNoteEvenF }));
            else add (aBalance, "Every phrase at its level", haveTraits && songHeard, 1.0f, haveTraits ? juce::String ("the leading parts hold their level phrase by phrase") : noTraits);
        }

        // ===== the low end against the kick: the bass's polarity, matched low cuts, ducks, punch =====
        {
            const auto* kick = firstOf (1);
            const auto* bass = firstOf (2);
            if (kick != nullptr && bass != nullptr)
            {
                const auto ph = suite::TrackTraits::layerPhase (*kick, *bass);
                if (ph.valid && ph.correlation < -0.3f)
                    add (aLowEnd, "The bass against the kick", true, ramp (ph.correlation, 0.0f, -0.6f), trackLabel (*bass) + " against the kick: correlation " + juce::String (ph.correlation, 2),
                         "the bass's polarity flipped (one click, or the autopilot)", -1, suite::Fix::TrackPolarity, bass->id & 0xfffu, false,
                         features ({ AP::mxLowPolarityF, AP::mxMatchedLowCutF, AP::mxBassLowDuckF, AP::mxDropPunchF, AP::mxKickClickF, AP::mxBassMidDriveF }));
                else
                {
                    // the bass's lows under the kick: the kick's hits stand out of the low end?
                    const auto overlap = bass->lowMidDb - kick->lowMidDb;   // the bass louder than the kick under 150 Hz
                    add (aLowEnd, "The bass against the kick", ph.valid || kick->hitCount >= 8, ramp (overlap, 6.0f, 12.0f),
                         overlap > 6.0f ? trackLabel (*bass) + "'s lows sit " + db (overlap) + " over the kick's" : juce::String ("the kick and bass share the lows"),
                         "the bass's lows duck under each kick, kick and bass low cuts matched (the autopilot)", -1, suite::Fix::None, 0, false,
                         features ({ AP::mxBassLowDuckF, AP::mxMatchedLowCutF, AP::mxDropPunchF, AP::mxKickClickF, AP::mxBassMidDriveF, AP::mxAutoFitKickF, AP::mxPumpSectionF }));
                }
            }
            else add (aLowEnd, "The bass against the kick", false, 1.0f, haveTraits ? juce::String ("no kick and bass both measured yet") : noTraits);
        }

        // ===== the kick per section: as loud against the mix in every section it plays =====
        if (const auto* kick = firstOf (1); kick != nullptr && songHeard && ! drops.empty())
        {
            const auto* d = secOf (drops[0]);
            const auto dropRel = d != nullptr && d->heard() && kick->sectionDb[(size_t) juce::jlimit (0, suite::TrackTraits::maxSections - 1, d->index)] > -60.0f
                                   ? kick->sectionDb[(size_t) d->index] - d->db : -100.0f;
            float worst = 0.0f; int worstType = -1, worstBar = -1;
            if (dropRel > -90.0f)
                for (const auto& s : S)
                {
                    if (! s.heard() || s.type == T::Drop || s.type == T::Breakdown || s.index >= suite::TrackTraits::maxSections) continue;
                    const auto k = kick->sectionDb[(size_t) s.index];
                    if (k < -60.0f) continue;
                    const auto rel = (k - s.db) - dropRel;
                    if (std::abs (rel) > std::abs (worst)) { worst = rel; worstType = (int) s.type; worstBar = s.startBar; }
                }
            if (worstType >= 0)
                add (aBalance, "The kick per section", true, ramp (std::abs (worst), 1.5f, 4.0f),
                     "the kick in the " + juce::String (sectionName ((T) worstType)).toLowerCase() + " sits " + db (worst) + " against the mix compared with the drop",
                     "the kick's level in that kind of section (one click, or the autopilot)", worstBar, suite::Fix::SectionTrim,
                     suite::packSectionTrim (kick->id, worstType, worst > 0.0f ? -1.0f : 1.0f), true, features ({ AP::mxKickSectionF }));
            else add (aBalance, "The kick per section", dropRel > -90.0f, 1.0f, "the kick holds its place in every section");
        }

        // ===== the low end's headroom: the lows' share of the drop against the label =====
        if (! drops.empty())
            if (const auto* d = secOf (drops[0]); d != nullptr && d->heard())
            {
                const auto sh = shape (d->band);
                const auto& st = P.tone[(size_t) T::Drop][1];
                const auto* bass = firstOf (2);
                if (st.valid() && bass != nullptr)
                {
                    const auto over = sh[1] - st.median;
                    add (aLowEnd, "Headroom in the low end", true, ramp (over, 2.0f, 5.0f), "the drop's 63 Hz band " + db (over) + " against the label",
                         "the bass a step down at 63 Hz in the drops (one click, or the autopilot)", d->startBar, suite::Fix::SectionTone,
                         suite::packSectionTone (bass->id, (int) T::Drop, 1, over > 0.0f ? -1.0f : 0.5f), true, features ({ AP::mxLfHeadroomF, AP::mxDcBlockF }));
                }
                else add (aLowEnd, "Headroom in the low end", false, 1.0f, st.valid() ? juce::String ("no bass measured yet") : juce::String ("the label's low end isn't known"));
            }

        // ===== parts collided in the centre: two narrow parts of one kind on top of each other =====
        {
            const suite::TrackTraits::Traits* a = nullptr; const suite::TrackTraits::Traits* b = nullptr;
            for (size_t i = 0; i < in.traits.size() && b == nullptr; ++i)
                for (size_t j = i + 1; j < in.traits.size() && b == nullptr; ++j)
                {
                    const auto& x = in.traits[i]; const auto& y = in.traits[j];
                    if (x.role != y.role || (x.role != 3 && x.role != 4) || ! heard (x) || ! heard (y)) continue;
                    if (x.sideDb < x.outRmsDb - 20.0f && y.sideDb < y.outRmsDb - 20.0f && std::abs (x.outRmsDb - y.outRmsDb) < 6.0f) { a = &x; b = &y; }
                }
            if (b != nullptr)
                add (aMids, "Parts side by side", true, 0.5f, trackLabel (*a) + " and " + trackLabel (*b) + " both sit in the centre",
                     "one panned a little aside, mono parts widened mono-safely (one click, or the autopilot)", -1, suite::Fix::TrackPan,
                     suite::packMasterTone (b->id, 0, 2.0f), true,
                     features ({ AP::mxPanLayoutF, AP::mxMonoWidenF, AP::mxBandWidthF, AP::mxCorrelationGuardF, AP::mxSideAirF, AP::mxSideMudF }));
            else add (aMids, "Parts side by side", haveTraits, 1.0f, haveTraits ? juce::String ("no two parts of one kind collide in the centre") : noTraits);
        }

        // ===== room for the lead and the vocal: they sit over the pads in the drops =====
        if (! drops.empty() && haveTraits)
        {
            const auto di = drops[0];
            const suite::TrackTraits::Traits* front = nullptr;
            for (const auto* t : { firstOf (6), firstOf (5) }) if (t != nullptr && t->sectionDb[(size_t) juce::jlimit (0, suite::TrackTraits::maxSections - 1, di)] > -60.0f) { front = t; break; }
            float padTop = -100.0f;
            for (const auto& t : in.traits) if ((t.role == 4 || t.role == 3 || t.role == 7) && heard (t) && di < suite::TrackTraits::maxSections) padTop = juce::jmax (padTop, t.sectionDb[(size_t) di]);
            if (front != nullptr && padTop > -60.0f)
            {
                const auto over = front->sectionDb[(size_t) di] - padTop;
                add (aMids, "Room for the lead and vocal", true, ramp (over, 4.0f, 0.0f), trackLabel (*front) + " sits " + db (over) + " over the loudest backing part in the drop",
                     "the backing parts make room around the lead's note, the hook's rhythm and the vocal's presence (the autopilot)", -1, suite::Fix::None, 0, false,
                     features ({ AP::mxPadUnderLeadF, AP::mxArpUnderHookF, AP::mxBackingHighCutF, AP::mxChordFollowF, AP::mxFxUnderVocalF, AP::mxClapUnderVocalF,
                                 AP::mxHatsUnderSibilanceF, AP::mxVocalPresenceF, AP::mxVocalAirF, AP::mxVocalPeakF, AP::mxVocalDoublerF, AP::mxHookLiftF,
                                 AP::mxPresenceEaseF, AP::mxDepthSectionF }));
            }
            else add (aMids, "Room for the lead and vocal", false, 1.0f, "no lead or vocal measured in the drop yet");
        }

        // ===== the drop's impact: its first bar against the build's last =====
        if (! drops.empty() && songHeard)
        {
            const auto* d = secOf (drops[0]);
            const SectionStat* before = nullptr;
            for (const auto& s : S) if (d != nullptr && s.index == d->index - 1) before = &s;
            if (d != nullptr && before != nullptr && d->firstDb > -90.0f && before->lastDb > -90.0f)
            {
                const auto jump = d->firstDb - before->lastDb;
                add (aAR, "The drop lands", true, ramp (jump, 3.0f, 0.0f), "the drop's first bars " + db (jump) + " over the end of the " + juce::String (sectionName (before->type)).toLowerCase(),
                     "the space tightens and the high-pass rises into the drop, the riser gets room, the drop's first beat hits (the autopilot)", d->startBar, suite::Fix::None, 0, false,
                     features ({ AP::mxImpactDuckF, AP::mxDropPunchF, AP::mxVerbGateF, AP::mxBuildHighPassF, AP::mxRiserRoomF, AP::mxRiserEnergyF,
                                 AP::mxFinalDropLiftF, AP::mxPostDropSettleF }));
            }
            else add (aAR, "The drop lands", false, 1.0f, "no section heard before the first drop yet");
        }

        // ===== the breakdowns open up: lighter, wider, more space than the drops =====
        if (! drops.empty() && ! breaks.empty() && songHeard)
        {
            const auto* d = secOf (drops[0]); const auto* b = secOf (breaks[0]);
            if (d != nullptr && b != nullptr && d->heard() && b->heard())
            {
                const auto contrast = d->db - b->db;
                add (aAR, "The breakdowns open up", true, ramp (contrast, 4.0f, 1.0f), "the breakdown sits " + db (-contrast) + " against the drop",
                     "drums thinned, sends and decay up, the pump and drive eased in the breakdowns (the autopilot)", b->startBar, suite::Fix::None, 0, false,
                     features ({ AP::mxBreakdownDrumLowCutF, AP::mxBreakdownDrumHighCutF, AP::mxUpwardBreakdownF, AP::mxSendsSectionF, AP::mxDecaySectionF,
                                 AP::mxEchoDampF, AP::mxDepthSectionF, AP::mxPumpSectionF, AP::mxDriveSectionF, AP::mxIntroOutroToneF }));
            }
        }

        // ===== the drums' dynamics: hits even, tails tidy, the bus glued =====
        {
            const suite::TrackTraits::Traits* worst = nullptr;
            for (const auto& t : in.traits) if ((t.role == 1 || t.role == 3) && t.hitCount >= 8 && (worst == nullptr || t.hitSpreadDb > worst->hitSpreadDb)) worst = &t;
            if (worst != nullptr)
                add (aMids, "The drums hold together", true, ramp (worst->hitSpreadDb, 4.0f, 9.0f), trackLabel (*worst) + "'s hits spread " + juce::String (worst->hitSpreadDb, 1) + " dB",
                     "parallel compression and glue on the drum bus, clap tails and pad sustain held back in the drops (the autopilot)", -1, suite::Fix::None, 0, false,
                     features ({ AP::mxDrumParallelF, AP::mxGluePerSectionF, AP::mxBusSatDropF, AP::mxClapTailF, AP::mxPadSustainF, AP::mxHatRidesF, AP::mxFillDuckF,
                                 AP::mxExpanderF }));
            else add (aMids, "The drums hold together", false, 1.0f, haveTraits ? juce::String ("not enough drum hits heard yet") : noTraits);
        }

        // ===== a clean signal: no DC, no peaks between the samples, nothing under the lowest note =====
        {
            const suite::TrackTraits::Traits* dc = nullptr; const suite::TrackTraits::Traits* hot = nullptr; const suite::TrackTraits::Traits* low = nullptr;
            float hotDb = -100.0f;
            for (const auto& t : in.traits)
            {
                if (! heard (t)) continue;
                if (t.dcDb > -40.0f && (dc == nullptr || t.dcDb > dc->dcDb)) dc = &t;
                for (auto p : t.sectionPeakDb) if (p > hotDb) { hotDb = p; hot = &t; }
                if (t.role >= 3 && t.lowestNote > 0 && t.lowMidDb > t.outRmsDb - 12.0f && suite::music::midiToHz ((float) t.lowestNote) > 200.0f) low = &t;
            }
            const auto bad = dc != nullptr ? 0.3f : hotDb > -1.0f ? 0.5f : low != nullptr ? 0.6f : 1.0f;
            const auto what = dc != nullptr ? trackLabel (*dc) + " carries DC (" + db (dc->dcDb) + ")"
                            : hotDb > -1.0f && hot != nullptr ? trackLabel (*hot) + " peaks at " + db (hotDb) + " (inter-sample peaks likely)"
                            : low != nullptr ? trackLabel (*low) + " has lows under its lowest note" : juce::String ("no DC, no hot peaks, nothing under the parts' lowest notes");
            add (aHygiene, "A clean signal", haveTraits, bad, haveTraits ? what : noTraits, "DC removed, inter-sample peaks trimmed at the source, a low cut from each part's lowest note (the autopilot)",
                 -1, suite::Fix::None, 0, false, features ({ AP::mxDcBlockF, AP::mxTruePeakF, AP::mxAutoLowCutF }));
        }

        // ===== the top end: air on the hats and pads, the FX kept out of the lows and darkening =====
        if (! drops.empty())
            if (const auto* d = secOf (drops[0]); d != nullptr && d->heard())
            {
                const auto sh = shape (d->band);
                const auto& st = P.tone[(size_t) T::Drop][8];
                if (st.valid())
                {
                    const auto under = st.median - sh[8];
                    add (aMids, "Air in the drops", true, ramp (under, 2.0f, 5.0f), "the drop's 8 kHz band " + db (-under) + " against the label",
                         "an exciter on the hats and pads, the FX high-passed and their tails darkening (the autopilot)", d->startBar, suite::Fix::None, 0, false,
                         features ({ AP::mxExciterAirF, AP::mxFxHighPassF, AP::mxFxTailDarkF }));
                }
            }

        // ===== the space by role: the lead and vocal clear of their own reverb =====
        {
            const suite::TrackTraits::Traits* worst = nullptr; float over = 0.0f;
            const auto beatMs = 60000.0f / (float) juce::jlimit (60.0, 200.0, in.bpm);
            for (const auto& t : in.traits)
                if ((t.role == 5 || t.role == 6 || t.role == 4) && t.decayCount >= 5)
                    if (const auto o = 3.0f * t.decayMs / ((t.role == 4 ? 4.0f : 2.0f) * beatMs); o > over) { over = o; worst = &t; }
            if (worst != nullptr)
                add (aMids, "The space by role", true, ramp (over, 0.8f, 1.5f), trackLabel (*worst) + "'s tail rings " + juce::String (over, 1) + "x its room",
                     "pre-delay and the echo's division by role, the pads' own room, a clap's reverb from the tempo (the autopilot)", -1, suite::Fix::None, 0, false,
                     features ({ AP::mxRolePreDelayF, AP::mxDelayDivisionF, AP::mxPadRoomF, AP::mxClapVerbF, AP::mxSendsSectionF }));
        }
    }
}
