#pragma once

// More label-ready checks, from what every Quintessence measures on its own track (TrackTraits)
// and from the written parts: the gaps the first set left unmeasured (sidechain recovery, layer
// phase, stereo lows per track, reverb sizes, depth, supersaw stacking, levels left behind by
// automation, clip gain), the mix moving toward the label (kick and bass paired, the mud budget,
// the lead on top in every section, width that survives mono), and writing intelligence (chord
// freshness, the groove's signature, singability, room for a vocal, the key change's fit).
// Included at the end of LabelReady.h; evaluate() calls extraChecks().

namespace lab
{
    namespace detail
    {
        inline bool isLayerRole (int role) { return role == 2 || role == 4 || role == 5 || ((role == 1 || role == 3) && suite::Autopilot::on (suite::Autopilot::mxLayerRolesF)); }   // kick and hat layers too (the autopilot)
        inline juce::String trackLabel (const suite::TrackTraits::Traits& t)
        {
            return t.trackName().isNotEmpty() ? t.trackName() : juce::String (suite::director::roleName (t.role));
        }
        /** The key change that suits every part best: the semitones up (0..5), and why. */
        inline int bestLift (const suite::SongBus::Song& song, juce::String& why)
        {
            int hookHi = 0, bassLo = 127, bassHi = 0;
            for (const auto& n : song.hook) hookHi = juce::jmax (hookHi, n.pitch);
            for (const auto& n : song.counter) hookHi = juce::jmax (hookHi, n.pitch);
            for (const auto& n : song.bass) { bassLo = juce::jmin (bassLo, n.pitch); bassHi = juce::jmax (bassHi, n.pitch); }
            int best = 0; float bestScore = -1.0f;
            for (int lift : { 1, 2, 3, 5 })
            {
                float sc = 1.0f;
                if (hookHi > 0 && hookHi + lift > 91) sc -= 0.15f * (float) (hookHi + lift - 91);   // over G6: thin and shrill
                if (bassHi > 0 && bassHi + lift > 55) sc -= 0.1f * (float) (bassHi + lift - 55);    // the bass climbing into the low mids
                sc += lift == 2 ? 0.1f : lift == 1 ? 0.05f : 0.0f;                                  // a whole tone lifts most, a semitone next
                if (sc > bestScore) { bestScore = sc; best = lift; }
            }
            why = "the hook tops out at MIDI " + juce::String (hookHi + best) + ", the bass at " + juce::String (bassHi + best);
            juce::ignoreUnused (bassLo);
            return best;
        }
    }

    inline void extraChecks (const Inputs& in, const Profile& P, std::vector<Check>& out)
    {
        using namespace detail;
        auto add = [&out] (int area, const juce::String& name, bool measured, float score, const juce::String& detail, const juce::String& explain = {}, int bar = -1,
                           bool blocking = false, suite::Fix fix = suite::Fix::None, uint32_t data = 0, bool safe = false, const juce::String& action = {})
        {
            Check c;
            c.area = area; c.name = name; c.measured = measured; c.score = juce::jlimit (0.0f, 1.0f, score); c.detail = detail; c.explain = explain;
            c.bar = bar; c.blocking = blocking; c.fix = fix; c.fixData = data; c.safeFix = safe; c.action = action;
            out.push_back (c);
        };
        const auto& S = in.sections;
        const auto beatMs = 60000.0f / (float) juce::jlimit (60.0, 200.0, in.bpm);
        const auto haveTraits = ! in.traits.empty();
        const auto noTraits = juce::String ("put Quintessence on the tracks (it measures each one)");
        auto heardTrack = [] (const suite::TrackTraits::Traits& t) { return t.outRmsDb > -60.0f; };

        // ===== the gaps: low end =====
        {
            // sidechain recovery: ducked tracks come back in time for the next beat, not instantly
            const suite::TrackTraits::Traits* worst = nullptr; float worstScore = 1.0f, worstMs = 0.0f;
            for (const auto& t : in.traits)
            {
                if (t.duckCount < 8 || t.role == 1 || t.role == 3) continue;
                const auto beats = t.recoveryMs / beatMs;
                const auto sc = juce::jmin (ramp (beats, 0.3f, 0.12f), ramp (beats, 0.65f, 0.95f));
                if (sc < worstScore || worst == nullptr) { worstScore = sc; worst = &t; worstMs = t.recoveryMs; }
            }
            if (worst != nullptr)
                add (aLowEnd, "Sidechain recovery", true, worstScore, trackLabel (*worst) + " comes back in " + juce::String (juce::roundToInt (worstMs)) + " ms ("
                     + juce::String (worstMs / beatMs, 2) + " of a beat)", worstMs / beatMs > 0.65f ? juce::String ("a shorter release: it should be back before the next kick (Kybalion: Fit to kick)")
                                                                                                     : juce::String ("a longer release: an instant snap-back clicks (Kybalion: Fit to kick)"),
                     -1, false, suite::Fix::KickFit, 0, true);
            else add (aLowEnd, "Sidechain recovery", false, 1.0f, haveTraits ? juce::String ("nothing ducked yet (play a drop)") : noTraits);

            // stereo lows, track by track
            const suite::TrackTraits::Traits* wide = nullptr; float wideBy = -100.0f;
            for (const auto& t : in.traits)
            {
                if (t.lowMidDb < -50.0f || ! heardTrack (t)) continue;
                const auto d = t.lowSideDb - t.lowMidDb;
                if (d > wideBy) { wideBy = d; wide = &t; }
            }
            if (wide != nullptr)
                add (aLowEnd, "Stereo lows per track", true, ramp (wideBy, -18.0f, -8.0f), trackLabel (*wide) + ": the side under 150 Hz " + db (wideBy) + " against its middle",
                     "mono under 150 Hz on that track (the autopilot's mono lows)", -1, false, suite::Fix::None, 0, false,
                     "autopilot:" + juce::String ((int) suite::Autopilot::monoLowsF));
            else add (aLowEnd, "Stereo lows per track", false, 1.0f, haveTraits ? juce::String ("no low end heard yet") : noTraits);

            // layers of one part in phase with each other
            float worstCorr = 1.0f; const suite::TrackTraits::Traits* flipMe = nullptr; juce::String pair; float lagMs = 0.0f, best = 1.0f;
            for (size_t i = 0; i < in.traits.size(); ++i)
                for (size_t j = i + 1; j < in.traits.size(); ++j)
                {
                    const auto& a = in.traits[i]; const auto& b = in.traits[j];
                    if (a.role != b.role || ! isLayerRole (a.role)) continue;
                    const auto ph = suite::TrackTraits::layerPhase (a, b);
                    if (! ph.valid) continue;
                    if (ph.correlation < worstCorr) { worstCorr = ph.correlation; flipMe = &b; pair = trackLabel (a) + " and " + trackLabel (b); lagMs = ph.lagMs; best = ph.bestCorrelation; }
                }
            if (flipMe != nullptr)
            {
                const auto flipped = worstCorr < -0.3f;
                const auto late = ! flipped && best - worstCorr > 0.3f && std::abs (lagMs) >= 0.5f;
                add (aLowEnd, "Layers in phase", true, flipped ? ramp (worstCorr, 0.0f, -0.6f) : late ? 0.6f : 1.0f,
                     pair + ": correlation " + juce::String (worstCorr, 2) + (late ? ", " + juce::String (std::abs (lagMs), 1) + " ms apart" : juce::String()),
                     flipped ? "flip one layer's polarity (one click)" : late ? juce::String ("line the layers up (the autopilot's layer alignment)") : juce::String ("they add up"),
                     -1, false, flipped ? suite::Fix::TrackPolarity : suite::Fix::None, flipped ? (flipMe->id & 0xfffu) : 0u, false,
                     late ? "autopilot:" + juce::String ((int) suite::Autopilot::layerAlignF) : juce::String());
            }
            else add (aLowEnd, "Layers in phase", false, 1.0f, haveTraits ? juce::String ("no two layers of one part playing together yet") : noTraits);
        }

        // ===== the gaps: mids, top and space =====
        {
            // reverb sizes: tails against the tempo (pads and FX may ring a bar, leads half of one)
            const suite::TrackTraits::Traits* longest = nullptr; float over = -10.0f, rtShown = 0.0f;
            for (const auto& t : in.traits)
            {
                if (t.decayCount < 5 || t.role == 1) continue;
                const auto rt60 = 3.0f * t.decayMs;
                const auto limitBeats = t.role == 4 || t.role == 7 ? 4.0f : t.role == 5 || t.role == 6 ? 2.0f : 1.0f;
                const auto o = rt60 / (limitBeats * beatMs);
                if (o > over) { over = o; longest = &t; rtShown = rt60; }
            }
            if (longest != nullptr)
                add (aMids, "Reverb sizes", true, ramp (over, 1.0f, 2.0f), trackLabel (*longest) + " rings about " + juce::String (rtShown / 1000.0f, 1) + " s (" + juce::String (rtShown / beatMs, 1) + " beats)",
                     "a shorter reverb, timed to the tempo (the autopilot)", -1, false, suite::Fix::None, 0, false, "autopilot:" + juce::String ((int) suite::Autopilot::tempoSpaceF));
            else add (aMids, "Reverb sizes", false, 1.0f, haveTraits ? juce::String ("not enough separate hits heard yet") : noTraits);

            // depth: the lead and vocal in front of the pads (spikier, drier, narrower)
            auto depthOf = [] (const suite::TrackTraits::Traits& t)
            {
                const auto wideness = juce::jlimit (0.0f, 1.0f, (t.sideDb - t.outRmsDb + 20.0f) / 20.0f);
                const auto softness = juce::jlimit (0.0f, 1.0f, 1.0f - (t.crestDb - 6.0f) / 12.0f);
                return 0.5f * wideness + 0.5f * softness;   // 0 = right in front, 1 = far back
            };
            float front = -1.0f, back = -1.0f; juce::String frontName, backName;
            for (const auto& t : in.traits)
            {
                if (! heardTrack (t)) continue;
                const auto d = depthOf (t);
                if ((t.role == 5 || t.role == 6) && d > front) { front = d; frontName = trackLabel (t); }
                if (t.role == 4 && (back < 0.0f || d < back)) { back = d; backName = trackLabel (t); }
            }
            if (front >= 0.0f && back >= 0.0f)
                add (aMids, "Depth: the lead in front", true, ramp (back - front, 0.15f, -0.1f), frontName + " sits " + (front <= back ? "in front of " : "behind ") + backName
                     + " (" + juce::String (juce::roundToInt (front * 100.0f)) + " vs " + juce::String (juce::roundToInt (back * 100.0f)) + ", 0 = right in front)",
                     "the lead drier and narrower, the pads wider and softer: pre-delay on the reverb from the tempo (one click)", -1, false,
                     suite::Fix::PreDelayTempo, 0, true, "autopilot:" + juce::String ((int) suite::Autopilot::preDelayF));
            else add (aMids, "Depth: the lead in front", false, 1.0f, haveTraits ? juce::String ("needs a lead (or vocal) and a pad playing") : noTraits);

            // supersaw stacking: wide layers piling into one octave
            std::map<int, int> perBand;
            for (const auto& t : in.traits)
            {
                if ((t.role != 5 && t.role != 4) || ! heardTrack (t) || t.sideDb - t.outRmsDb < -8.0f) continue;
                for (const auto& r : in.tracks)
                    if (r.track == t.trackName())
                    {
                        int top = 4; for (int k = 4; k < numBands; ++k) if (r.bands[(size_t) k] > r.bands[(size_t) top]) top = k;
                        ++perBand[top];
                    }
            }
            int most = 0, mostBand = 0;
            for (const auto& [b, c] : perBand) if (c > most) { most = c; mostBand = b; }
            uint32_t weakest = 0; float weakestDb = 1000.0f;
            for (const auto& t : in.traits)
                if ((t.role == 5 || t.role == 4) && heardTrack (t) && t.sideDb - t.outRmsDb >= -8.0f && t.outRmsDb < weakestDb)
                    for (const auto& r : in.tracks)
                        if (r.track == t.trackName())
                        {
                            int top = 4; for (int k = 4; k < numBands; ++k) if (r.bands[(size_t) k] > r.bands[(size_t) top]) top = k;
                            if (top == mostBand && most >= 2) { weakest = t.id; weakestDb = t.outRmsDb; }
                        }
            if (! perBand.empty())
                add (aMids, "Supersaw stacking", true, ramp ((float) most, 2.0f, 4.0f), juce::String (most) + " wide layer(s) peaking around " + juce::String (juce::roundToInt (analysis::bandCentres()[(size_t) mostBand])) + " Hz",
                     "one wide layer per octave: the weaker one a step down there (one click, or the autopilot)", -1, false,
                     weakest != 0 ? suite::Fix::MasterTone : suite::Fix::None, weakest != 0 ? suite::packMasterTone (weakest, mostBand, -1.5f) : 0u, true,
                     "autopilot:" + juce::String ((int) suite::Autopilot::layerSplitF));
            else add (aMids, "Supersaw stacking", false, 1.0f, haveTraits ? juce::String ("no wide leads or pads heard yet") : noTraits);
        }

        // ===== the gaps: hygiene =====
        {
            // levels left behind by automation: a track much quieter (or louder) in one section than in
            // the other sections of the same kind
            juce::String found; int foundBar = -1; float foundBy = 0.0f;
            for (const auto& t : in.traits)
                for (int type = 0; type < numTypes; ++type)
                {
                    std::vector<std::pair<int, float>> same;
                    for (int i = 0; i < juce::jmin (in.map.count, suite::TrackTraits::maxSections); ++i)
                        if ((int) in.map.sections[(size_t) i].type == type && t.sectionDb[(size_t) i] > -90.0f) same.push_back ({ i, t.sectionDb[(size_t) i] });
                    if (same.size() < 2) continue;
                    std::vector<float> v; for (auto& [i, d] : same) v.push_back (d);
                    std::sort (v.begin(), v.end());
                    const auto median = v[v.size() / 2];
                    for (auto& [i, d] : same)
                        if (std::abs (d - median) > std::abs (foundBy))
                        {
                            foundBy = d - median; foundBar = in.map.sections[(size_t) i].startBar;
                            found = trackLabel (t) + " in the " + juce::String (suite::sectionName ((T) type)).toLowerCase() + " at bar " + juce::String (foundBar);
                        }
                }
            if (found.isNotEmpty())
                add (aHygiene, "Automation leftovers", true, ramp (std::abs (foundBy), 4.0f, 9.0f), found + " is " + db (foundBy) + " against its other ones",
                     "a filter or level automation left closed: check that section's automation", foundBar);
            else add (aHygiene, "Automation leftovers", false, 1.0f, haveTraits ? juce::String ("needs two sections of one kind heard") : noTraits);

            // clip gain: what comes into each track's chain
            const suite::TrackTraits::Traits* hot = nullptr; const suite::TrackTraits::Traits* quiet = nullptr;
            for (const auto& t : in.traits)
            {
                if (t.inRmsDb < -90.0f) continue;
                if (hot == nullptr || t.inPeakDb > hot->inPeakDb) hot = &t;
                if (heardTrack (t) && (quiet == nullptr || t.inRmsDb < quiet->inRmsDb)) quiet = &t;
            }
            if (hot != nullptr)
            {
                const auto hotScore = ramp (hot->inPeakDb, -3.0f, 0.5f), quietScore = quiet != nullptr ? ramp (quiet->inRmsDb, -40.0f, -55.0f) : 1.0f;
                add (aHygiene, "Clip gain", true, juce::jmin (hotScore, quietScore),
                     hotScore <= quietScore ? trackLabel (*hot) + " comes in peaking at " + db (hot->inPeakDb) : trackLabel (*quiet) + " comes in at " + db (quiet->inRmsDb) + " RMS",
                     hotScore <= quietScore ? juce::String ("turn the clip or the synth down so the chain gets headroom (peaks under -3 dBFS)")
                                            : juce::String ("bring the clip up so the chain's noise and thresholds sit right (about -18 dBFS RMS)"));
            }
            else add (aHygiene, "Clip gain", false, 1.0f, haveTraits ? juce::String ("nothing playing yet") : noTraits);
        }

        // ===== the mix moving toward the label =====
        {
            // kick and bass paired: the bass's root against the kick's pitch, the bass back as the kick dies
            const suite::TrackTraits::Traits* kick = nullptr; const suite::TrackTraits::Traits* bass = nullptr;
            for (const auto& t : in.traits) { if (t.role == 1 && (kick == nullptr || t.outRmsDb > kick->outRmsDb)) kick = &t; if (t.role == 2 && (bass == nullptr || t.outRmsDb > bass->outRmsDb)) bass = &t; }
            if (kick != nullptr && bass != nullptr && kick->decayCount >= 4 && bass->duckCount >= 4)
            {
                const auto ratio = bass->recoveryMs / juce::jmax (1.0f, kick->decayMs);
                float pitchFit = 1.0f; juce::String tune;
                if (in.kickHz > 0.0f && in.song.valid)
                {
                    const auto kickPc = ((juce::roundToInt (69.0f + 12.0f * std::log2 (in.kickHz / 440.0f))) % 12 + 12) % 12;
                    const auto d = ((in.song.root - kickPc) % 12 + 12) % 12;
                    pitchFit = d == 0 || d == 7 || d == 5 ? 1.0f : 0.6f;
                    tune = d == 0 ? juce::String (", the kick on the key's root") : ", the kick " + juce::String (d) + " semitones off the root";
                }
                add (aBalance, "Kick and bass paired", true, juce::jmin (pitchFit, juce::jmin (ramp (ratio, 0.8f, 0.4f), ramp (ratio, 1.6f, 2.5f))),
                     "the bass comes back in " + juce::String (juce::roundToInt (bass->recoveryMs)) + " ms, the kick dies in " + juce::String (juce::roundToInt (kick->decayMs)) + " ms" + tune,
                     "the duck as long as the kick's tail; tune the kick to the root or fifth (Kybalion: Fit to kick, the autopilot's kick tuning)",
                     -1, false, suite::Fix::KickFit, 0, true);
            }
            else add (aBalance, "Kick and bass paired", false, 1.0f, haveTraits ? juce::String ("needs a kick and a ducked bass playing") : noTraits);

            // the mud budget: 125-250 Hz belongs to the bass; anything else close to it there gives some back
            const suite::TrackTraits::Traits* bassT = bass;
            float bassMud = -100.0f;
            for (const auto& r : in.tracks) if (bassT != nullptr && r.track == bassT->trackName()) bassMud = powerSumDb (r.bands, 3, 4);
            if (bassMud > -90.0f)
            {
                juce::String culprit; float by = -100.0f; uint32_t id = 0;
                for (const auto& t : in.traits)
                {
                    if (t.role == 1 || t.role == 2) continue;
                    for (const auto& r : in.tracks)
                        if (r.track == t.trackName())
                            if (const auto d = powerSumDb (r.bands, 3, 4) - bassMud; d > by) { by = d; culprit = trackLabel (t); id = t.id; }
                }
                if (culprit.isNotEmpty())
                {
                    const auto cut = juce::jlimit (0.0f, 3.0f, by + 4.0f);
                    add (aMids, "Mud budget", true, ramp (by, -6.0f, 0.0f), culprit + " is " + db (by) + " against the bass at 125-250 Hz",
                         "it gives back " + juce::String (cut, 1) + " dB there (one click)", -1, false, cut > 0.25f ? suite::Fix::MasterTone : suite::Fix::None,
                         suite::packMasterTone (id, 3, -cut), true);
                }
            }

            // the lead on top in every section it plays: its margin over the mix kept from the drop
            const suite::TrackTraits::Traits* lead = nullptr;
            for (const auto& t : in.traits) if (t.role == 5 && (lead == nullptr || t.outRmsDb > lead->outRmsDb)) lead = &t;
            if (lead != nullptr && ! S.empty())
            {
                float dropMargin = -100.0f; std::vector<std::pair<int, float>> margins;
                for (const auto& s : S)
                {
                    if (! s.heard() || s.index >= suite::TrackTraits::maxSections || lead->sectionDb[(size_t) s.index] < -60.0f) continue;
                    const auto m = lead->sectionDb[(size_t) s.index] - s.db;
                    margins.push_back ({ s.index, m });
                    if (s.type == T::Drop) dropMargin = juce::jmax (dropMargin, m);
                }
                if (dropMargin > -90.0f && margins.size() >= 2)
                {
                    int worstI = -1; float worstDrop = 0.0f;
                    for (auto& [i, m] : margins) if (dropMargin - m > worstDrop) { worstDrop = dropMargin - m; worstI = i; }
                    if (worstI >= 0)
                    {
                        const auto type = (int) S[(size_t) worstI].type;
                        add (aBalance, "The lead on top in every section", true, ramp (worstDrop, 2.0f, 6.0f),
                             trackLabel (*lead) + " sits " + db (-worstDrop) + " lower against the mix in the " + juce::String (suite::sectionName ((T) type)).toLowerCase() + " than in the drop",
                             "ride it up there (one click: its level in those sections)", S[(size_t) worstI].startBar, false,
                             worstDrop > 1.0f ? suite::Fix::SectionTrim : suite::Fix::None, suite::packSectionTrim (lead->id, type, juce::jmin (3.0f, worstDrop - 1.0f)), true);
                    }
                    else add (aBalance, "The lead on top in every section", true, 1.0f, "it keeps its place over the mix everywhere it plays");
                }
            }

            // width that survives mono: the widest track narrowed, only when the mono sum loses
            if (in.master.valid && in.master.monoLossDb > 1.5f)
            {
                const suite::TrackTraits::Traits* widest = nullptr;
                for (const auto& t : in.traits) if (heardTrack (t) && t.role != 1 && t.role != 2 && (widest == nullptr || t.sideDb - t.outRmsDb > widest->sideDb - widest->outRmsDb)) widest = &t;
                if (widest != nullptr)
                    add (aTranslation, "Mono-safe width", true, ramp (in.master.monoLossDb, 1.5f, 4.0f), "the mono sum loses " + juce::String (in.master.monoLossDb, 1) + " dB; the widest part is " + trackLabel (*widest),
                         "that part narrower, nothing else (one click)", -1, false, suite::Fix::Narrower, widest->id & 0xfffu, true);
            }
        }

        // ===== build tension: the level, the density and the top end all climbing =====
        for (const auto& b : S)
        {
            if (b.type != T::Build || b.heardBars < 4) continue;
            const auto from = b.startBar - 1, to = from + b.bars, half = from + b.bars / 2;
            float onFirst = 0.0f, onLast = 0.0f; int nFirst = 0, nLast = 0;
            std::vector<float> hiFirst, hiLast;
            for (int k = from; k < to && k < (int) in.bars.size(); ++k)
            {
                const auto& x = in.bars[(size_t) k];
                if (! x.heard) continue;
                const auto hi = powerSumDb (x.band, 7, numBands - 1);
                if (k < half) { onFirst += x.onsets; ++nFirst; if (k < from + 2) hiFirst.push_back (hi); }
                else { onLast += x.onsets; ++nLast; if (k >= to - 2) hiLast.push_back (hi); }
            }
            if (nFirst == 0 || nLast == 0) continue;
            const auto rise = b.lastDb - b.firstDb;
            const auto dens = (onLast / (float) nLast) / juce::jmax (0.5f, onFirst / (float) nFirst);
            const auto bright = powerMeanDb (hiLast) - powerMeanDb (hiFirst);
            const auto sRise = P.buildRiseDb.valid() ? fromOutside (P.buildRiseDb.outside (rise)) : ramp (rise, 2.0f, 0.0f);
            const auto sDens = ramp (dens, 1.4f, 1.0f), sBright = ramp (bright, 3.0f, 0.0f);
            const auto score = (sRise + sDens + sBright) / 3.0f;
            const auto weakest = sRise <= sDens && sRise <= sBright ? 0 : sDens <= sBright ? 1 : 2;
            add (aAR, "Build tension at bar " + juce::String (b.startBar), true, score,
                 "level " + db (rise) + ", events x" + juce::String (dens, 1) + ", top end " + db (bright) + " from its start to its end",
                 weakest == 0 ? juce::String ("more rise in the last bars (the build's level)") : weakest == 1 ? juce::String ("more going on as it climbs: rolls and risers (the director)")
                                                                                                               : juce::String ("open the top end through it: the riser and the filters (the director)"),
                 b.startBar, false, weakest == 0 ? suite::Fix::BuildRise : suite::Fix::None, 0, weakest == 0,
                 weakest == 1 ? juce::String ("director: busier build") : weakest == 2 ? juce::String ("director: brighter build") : juce::String());
        }

        // ===== writing intelligence =====
        {
            const auto& h = in.song.hook;
            // chord freshness: the drop's progression against your other songs and the label's tracks
            if (in.song.valid && in.song.chordCount[(size_t) T::Drop] >= 2)
            {
                std::vector<int> roots;
                for (int c = 0; c < in.song.chordCount[(size_t) T::Drop]; ++c)
                    if (const auto r = in.song.chordRoots[(size_t) T::Drop][(size_t) c]; r >= 0 && (roots.empty() || roots.back() != r)) roots.push_back (r);
                if (roots.size() > 2 && roots.back() == roots.front()) roots.pop_back();
                const auto print = progressionPrint (roots);
                float mine = 0.0f, label = 0.0f; int labelSame = 0, labelN = 0;
                for (const auto& o : in.otherProgressions) mine = juce::jmax (mine, progressionSimilarity (print, o));
                for (const auto& r : P.refs) if (r.dropProgression.isNotEmpty()) { ++labelN; const auto sim = progressionSimilarity (print, r.dropProgression); label = juce::jmax (label, sim); labelSame += sim > 0.99f ? 1 : 0; }
                const auto stock = labelN >= 3 && (float) labelSame / (float) labelN > 0.5f;
                add (aWriting, "Chord freshness", print.isNotEmpty() && (! in.otherProgressions.empty() || labelN > 0), juce::jmin (ramp (mine, 0.7f, 1.0f), stock ? 0.7f : 1.0f),
                     juce::String (juce::roundToInt (mine * 100.0f)) + "% like your closest earlier song" + (labelN > 0 ? ", " + juce::String (juce::roundToInt (label * 100.0f)) + "% like the label's closest" : juce::String())
                         + (stock ? " (the label's stock progression)" : ""),
                     "a borrowed chord, a different starting chord, or a new loop (Correspondence: harmony ideas)");
            }
            // the groove's signature against your last tracks
            if (in.groovePrint.isNotEmpty() && ! in.otherGrooves.empty())
            {
                float closest = 0.0f;
                for (const auto& g : in.otherGrooves)
                {
                    int same = 0, n = juce::jmin (g.length(), in.groovePrint.length());
                    for (int i = 0; i < n; ++i) same += g[i] == in.groovePrint[i] ? 1 : 0;
                    if (n >= 16) closest = juce::jmax (closest, (float) same / (float) n);
                }
                add (aWriting, "The groove is its own", true, ramp (closest, 0.75f, 0.97f), juce::String (juce::roundToInt (closest * 100.0f)) + "% like your closest earlier groove",
                     "another kit, or move a stab or an arp accent (Correspondence: the kit)");
            }
            // singability: could a topliner sing the hook?
            if (h.size() >= 4)
            {
                int lo = 127, hi = 0, leaps = 0, breaths = 0;
                for (size_t i = 0; i < h.size(); ++i)
                {
                    lo = juce::jmin (lo, h[i].pitch); hi = juce::jmax (hi, h[i].pitch);
                    if (i > 0 && std::abs (h[i].pitch - h[i - 1].pitch) > 7) ++leaps;
                    if (i > 0 && h[i].start - (h[i - 1].start + h[i - 1].length) >= 0.5f) ++breaths;
                }
                const auto span = h.back().start + h.back().length - h.front().start;
                const auto breathsNeeded = juce::jmax (1, (int) (span / 8.0f));
                add (aWriting, "Singable hook", true, juce::jmin (ramp ((float) (hi - lo), 12.0f, 17.0f), juce::jmin (ramp ((float) leaps / (float) h.size(), 0.1f, 0.3f), ramp ((float) breaths, (float) breathsNeeded, 0.0f))),
                     juce::String (hi - lo) + " semitones, " + juce::String (leaps) + " leap(s) over a fifth, " + juce::String (breaths) + " breath(s)",
                     "within an octave and a bit, steps over leaps, a rest every two bars to breathe");
            }
            // room for a vocal: the breakdowns' and grooves' mids against the label's, and the hook's rests
            {
                float crowd = -100.0f; int bar = -1;
                for (const auto& s : S)
                {
                    if (! s.heard() || (s.type != T::Breakdown && s.type != T::Groove)) continue;
                    const auto sh = shape (s.band);
                    const auto& tn = P.tone[(size_t) s.type];
                    const auto o = juce::jmax (sh[5] - tn[5].hi, sh[6] - tn[6].hi, sh[7] - tn[7].hi);
                    if (o > crowd) { crowd = o; bar = s.startBar; }
                }
                float restShare = 1.0f;
                if (h.size() >= 4)
                {
                    double rests = 0.0, span = h.back().start + h.back().length - h.front().start;
                    for (size_t i = 1; i < h.size(); ++i) rests += juce::jmax (0.0, (double) h[i].start - (h[i - 1].start + h[i - 1].length));
                    restShare = span > 0.0 ? (float) (rests / span) : 1.0f;
                }
                if (bar > 0 || h.size() >= 4)
                    add (aWriting, "Room for a vocal", true, juce::jmin (bar > 0 ? ramp (crowd, 0.0f, 4.0f) : 1.0f, ramp (restShare, 0.15f, 0.03f)),
                         (bar > 0 ? "the mids (500 Hz-4 kHz) " + db (crowd) + " over the label in the quiet sections; " : juce::String())
                             + "the hook rests " + juce::String (juce::roundToInt (restShare * 100.0f)) + "% of the time",
                         "thin the pads' mids where a vocal would sit, leave gaps in the hook (Correspondence: vocal room)", bar);
            }
            // the key change: does every part still fit, and which lift would suit best
            if (in.song.valid && (! h.empty() || ! in.song.bass.empty()))
            {
                juce::String why;
                const auto best = bestLift (in.song, why);
                if (in.song.finalLift > 0)
                {
                    int hi = 0, bassHi = 0;
                    for (const auto& n : h) hi = juce::jmax (hi, n.pitch);
                    for (const auto& n : in.song.bass) bassHi = juce::jmax (bassHi, n.pitch);
                    const auto fits = hi + in.song.finalLift <= 91 && bassHi + in.song.finalLift <= 55;
                    add (aWriting, "The key change fits every part", true, fits ? 1.0f : 0.5f,
                         "+" + juce::String (in.song.finalLift) + " semitones: the hook tops out at MIDI " + juce::String (hi + in.song.finalLift) + ", the bass at " + juce::String (bassHi + in.song.finalLift),
                         fits ? juce::String ("it fits") : "a lift of +" + juce::String (best) + " suits better (" + why + ")", -1, false, suite::Fix::None, 0, false,
                         fits || best == in.song.finalLift ? juce::String() : "lift:" + juce::String (best));
                }
                else add (aWriting, "The key change fits every part", false, 1.0f, "no final-drop key change; if you add one, +" + juce::String (best) + " suits these parts best (" + why + ")",
                          {}, -1, false, suite::Fix::None, 0, false, "lift:" + juce::String (best));
            }
        }
    }
}
