#pragma once

// The sixth set of label-ready checks (the mix fixes' own triggers, each naming its switches):
// lopsided peaks on a track and on the master (phase rotation), each drop's own tone, clap and
// snare layers, the low end's mono per section, the dynamics per section, the mix's movement,
// transients against the label, left/right delays, earbuds (hard pans, side-only parts), the car
// (the low-mid boom), the club (the lowest octave), smooth section changes, and fixes piling up on
// one track. Included at the end of LabelReady.h; evaluate() calls checks6().

#include "MixFixBus.h"

namespace lab
{
    inline void checks6 (const Inputs& in, const Profile& P, std::vector<Check>& out)
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
        const auto haveTraits = ! in.traits.empty();
        const juce::String noTraits = "no Quintessence measuring yet";
        int heardBars = 0; for (const auto& b : in.bars) heardBars += b.heard ? 1 : 0;
        const auto songHeard = heardBars >= 16;
        std::vector<int> drops;
        for (const auto& s : S) if (s.type == T::Drop) drops.push_back (s.index);
        const auto secOf = [&S] (int index) -> const SectionStat* { for (const auto& s : S) if (s.index == index) return &s; return nullptr; };
        auto barsOf = [&in] (const SectionStat& s)
        {
            std::vector<const Bar*> v;
            for (int b = s.startBar - 1; b < s.startBar - 1 + s.bars && b < (int) in.bars.size(); ++b) if (b >= 0 && in.bars[(size_t) b].heard) v.push_back (&in.bars[(size_t) b]);
            return v;
        };

        // ===== lopsided peaks: on a track (kick, bass, a bus), and on the master =====
        {
            const suite::TrackTraits::Traits* worst = nullptr;
            for (const auto& t : in.traits)
                if (heard (t) && (t.role == 1 || t.role == 2 || t.role == 0) && t.asymDb > 1.5f && (worst == nullptr || t.asymDb > worst->asymDb)) worst = &t;
            if (worst != nullptr)
                add (aHygiene, "Lopsided peaks", true, ramp (worst->asymDb, 1.5f, 4.0f), trackLabel (*worst) + "'s peaks are " + juce::String (worst->asymDb, 1) + " dB higher on one side",
                     "phase rotation evens them (an all-pass: the headroom back, nothing compressed)", -1, suite::Fix::None, 0, false, features ({ AP::mx2PhaseRotateF }));
            else add (aHygiene, "Lopsided peaks", haveTraits, 1.0f, haveTraits ? juce::String ("no track's peaks lean to one side") : noTraits);
            std::vector<float> asym;
            for (int d : drops) if (const auto* s = secOf (d)) for (const auto* b : barsOf (*s)) if (b->asymDb > 0.0f) asym.push_back (b->asymDb);
            if (asym.size() >= 4)
            {
                std::sort (asym.begin(), asym.end());
                const auto med = asym[asym.size() / 2];
                add (aHygiene, "The master's peaks even", true, ramp (med, 1.5f, 4.0f), "the drops' peaks " + juce::String (med, 1) + " dB higher on one side",
                     "phase rotation on the kick and bass (the autopilot)", -1, suite::Fix::None, 0, false, features ({ AP::mx2PhaseRotateF }));
            }
        }

        // ===== each drop's own tone: a later drop that drifts from the label =====
        if (drops.size() >= 2)
        {
            const auto* d2 = secOf (drops[1]);
            const suite::TrackTraits::Traits* front = nullptr;
            for (const auto& t : in.traits) if ((t.role == 4 || t.role == 5) && heard (t) && (front == nullptr || t.outRmsDb > front->outRmsDb)) front = &t;
            if (d2 != nullptr && d2->heard() && front != nullptr)
            {
                const auto sh = shape (d2->band);
                float worst = 0.0f; int band = -1;
                for (int b = 1; b < numBands - 1; ++b)
                    if (const auto& st = P.tone[(size_t) T::Drop][(size_t) b]; st.valid() && std::abs (sh[(size_t) b] - st.median) > std::abs (worst)) { worst = sh[(size_t) b] - st.median; band = b; }
                if (band >= 0)
                    add (aMids, "Each drop's own tone", true, ramp (std::abs (worst), 2.0f, 5.0f),
                         "drop 2 at " + juce::String (juce::roundToInt (analysis::bandCentres()[(size_t) band])) + " Hz " + db (worst) + " against the label",
                         "a tone step for the later drops only on " + trackLabel (*front) + " (one click, or the autopilot)", d2->startBar, suite::Fix::SectionTone,
                         suite::packSectionTone (front->id, 6, band, worst > 0.0f ? -1.0f : 1.0f), true, features ({ AP::mx2LaterDropToneF }));
            }
        }

        // ===== clap and snare layers: in phase, lined up =====
        {
            auto isClap = [] (const suite::TrackTraits::Traits& t) { const auto n = t.trackName().toLowerCase(); return n.contains ("clap") || n.contains ("snare"); };
            const suite::TrackTraits::Traits* flipMe = nullptr; float worst = 1.0f, lag = 0.0f; juce::String pair; bool found = false;
            for (size_t i = 0; i < in.traits.size(); ++i)
                for (size_t j = i + 1; j < in.traits.size(); ++j)
                {
                    const auto& a = in.traits[i]; const auto& b = in.traits[j];
                    if (! isClap (a) || ! isClap (b)) continue;
                    const auto ph = suite::TrackTraits::layerPhase (a, b);
                    if (! ph.valid) continue;
                    found = true;
                    if (ph.correlation < worst) { worst = ph.correlation; flipMe = &b; pair = trackLabel (a) + " and " + trackLabel (b); lag = ph.lagMs; }
                }
            if (flipMe != nullptr)
            {
                const auto flipped = worst < -0.3f, late = ! flipped && std::abs (lag) >= 0.5f;
                add (aMids, "Clap layers together", true, flipped ? ramp (worst, 0.0f, -0.6f) : late ? 0.6f : 1.0f,
                     pair + ": correlation " + juce::String (worst, 2) + (late ? ", " + juce::String (std::abs (lag), 1) + " ms apart" : juce::String()),
                     flipped ? "one layer's polarity flipped (one click, or the autopilot)" : late ? juce::String ("the layers lined up (the autopilot)") : juce::String ("they hit together"),
                     -1, flipped ? suite::Fix::TrackPolarity : suite::Fix::None, flipped ? (flipMe->id & 0xfffu) : 0u, false,
                     features ({ AP::mx2ClapLayersF, AP::layerAlignF, AP::mxLayerRolesF }));
            }
            else add (aMids, "Clap layers together", found, 1.0f, "no two clap or snare layers measured together yet");
        }

        // ===== the low end's mono, section by section =====
        if (songHeard)
        {
            float worst = 1.0f; int worstType = -1, worstBar = -1;
            for (const auto& s : S)
            {
                double c = 0.0; int n = 0;
                for (const auto* b : barsOf (s)) if (b->lowCorrCount > 0) { c += b->lowCorr; ++n; }
                if (n >= 2 && (float) (c / n) < worst) { worst = (float) (c / n); worstType = (int) s.type; worstBar = s.startBar; }
            }
            if (worstType >= 0)
                add (aLowEnd, "The lows mono in every section", true, ramp (worst, 0.9f, 0.5f),
                     "under 120 Hz in the " + juce::String (sectionName ((T) worstType)).toLowerCase() + ": left and right correlate " + juce::String (worst, 2),
                     "the mono cutoff per section, mono lows (the autopilot)", worstBar, suite::Fix::None, 0, false, features ({ AP::mx2SideLowsSectionF, AP::monoLowsF }));
        }

        // ===== the dynamics per section: the drops' peak over their level =====
        if (! drops.empty())
            if (const auto* d = secOf (drops[0]); d != nullptr && d->heard())
            {
                std::vector<float> crest;
                for (const auto* b : barsOf (*d)) if (b->peakDb > -90.0f && b->db > -90.0f) crest.push_back (b->peakDb - b->db);
                if (crest.size() >= 4)
                {
                    std::sort (crest.begin(), crest.end());
                    const auto med = crest[crest.size() / 2];
                    // an unmastered trance drop at -6 dBFS peaks: about 9 to 14 dB of crest
                    const auto score = med < 9.0f ? ramp (med, 9.0f, 6.0f) : ramp (med, 14.0f, 18.0f);
                    add (aMids, "Dynamics in the drops", true, score, "the drops peak " + juce::String (med, 1) + " dB over their level (about 9 to 14 is usual unmastered)",
                         med < 9.0f ? juce::String ("squashed: the glue and parallel compression ease off (the autopilot)") : juce::String ("spiky: the drums and the vocal's peaks held (the autopilot)"), d->startBar,
                         suite::Fix::None, 0, false,
                         med < 9.0f ? features ({ AP::squashF, AP::mxGluePerSectionF }) : features ({ AP::mxDrumParallelF, AP::mxVocalPeakF, AP::mxGluePerSectionF, AP::mx2PhaseRotateF }));
                }
            }

        // ===== the mix's movement: the tone changing bar to bar in the drops =====
        if (! drops.empty())
            if (const auto* d = secOf (drops[0]); d != nullptr && d->heard())
            {
                const auto bs = barsOf (*d);
                double move = 0.0; int n = 0;
                for (size_t i = 1; i < bs.size(); ++i)
                {
                    const auto a = shape (bs[i - 1]->band), b = shape (bs[i]->band);
                    float m = 0.0f; for (int k = 0; k < numBands; ++k) m += std::abs (a[(size_t) k] - b[(size_t) k]);
                    move += m / (float) numBands; ++n;
                }
                if (n >= 4)
                {
                    const auto mv = (float) (move / n);
                    add (aAR, "The mix moves", true, ramp (mv, 0.6f, 0.2f), "the drop's tone changes " + juce::String (mv, 2) + " dB a bar",
                         "movement on static parts, hat rides, pad drive rising through the builds (the autopilot)", d->startBar, suite::Fix::None, 0, false,
                         features ({ AP::motionF, AP::padMotionF, AP::mxHatRidesF, AP::mx2BuildTensionF, AP::mx2RepeatToneF }));
                }
            }

        // ===== transients against the label: new hits a bar in the drops =====
        if (! drops.empty() && P.onsetsPerMinute.valid())
            if (const auto* d = secOf (drops[0]); d != nullptr && d->heard() && d->onsetsPerBar > 0.0f)
            {
                const auto barsPerMinute = (float) (in.bpm / juce::jmax (1.0, in.barQ));
                const auto perMin = d->onsetsPerBar * barsPerMinute;
                add (aMids, "Transients like the label", true, fromOutside (P.onsetsPerMinute.outside (perMin)),
                     juce::String (perMin, 0) + " hits a minute in the drop (the label: " + P.onsetsPerMinute.text (0) + ")",
                     "a click on a dull kick, ghost notes evened, drum bus parallel compression (the autopilot)", d->startBar, suite::Fix::None, 0, false,
                     features ({ AP::mxKickClickF, AP::mx2GhostEvenF, AP::mxDrumParallelF }));
            }

        // ===== left/right delays: a part combing in mono =====
        {
            const suite::TrackTraits::Traits* worst = nullptr;
            for (const auto& t : in.traits) if (heard (t) && std::abs (t.haasMs) > 0.2f && (worst == nullptr || std::abs (t.haasMs) > std::abs (worst->haasMs))) worst = &t;
            if (worst != nullptr)
                add (aTranslation, "No left/right delays", true, ramp (std::abs (worst->haasMs), 0.3f, 3.0f), trackLabel (*worst) + ": one side " + juce::String (std::abs (worst->haasMs), 1) + " ms behind the other (it combs in mono)",
                     "the sides realigned (the autopilot)", -1, suite::Fix::None, 0, false, features ({ AP::mx2HaasF }));
            else add (aTranslation, "No left/right delays", haveTraits, 1.0f, haveTraits ? juce::String ("no part has one side delayed") : noTraits);
        }

        // ===== earbuds: hard pans and parts only in the sides =====
        {
            const suite::TrackTraits::Traits* worst = nullptr; float bad = 0.0f; juce::String why;
            for (const auto& t : in.traits)
            {
                if (! heard (t)) continue;
                if (std::abs (t.lrBalanceDb) > 10.0f && std::abs (t.lrBalanceDb) > bad) { bad = std::abs (t.lrBalanceDb); worst = &t; why = "panned " + juce::String (std::abs (t.lrBalanceDb), 0) + " dB to one side"; }
                if (t.sideDb > t.outRmsDb && t.sideDb - t.outRmsDb + 10.0f > bad) { bad = t.sideDb - t.outRmsDb + 10.0f; worst = &t; why = "mostly in the sides (it vanishes in mono and hurts in earbuds)"; }
            }
            if (worst != nullptr)
                add (aTranslation, "Earbuds", true, ramp (bad, 10.0f, 18.0f), trackLabel (*worst) + ": " + why, "nothing past 70%, the side kept in check (the autopilot)", -1,
                     suite::Fix::None, 0, false, features ({ AP::mx2PanLimitF, AP::mxCorrelationGuardF }));
            else add (aTranslation, "Earbuds", haveTraits, 1.0f, haveTraits ? juce::String ("no hard pans, nothing only in the sides") : noTraits);
        }

        // ===== the car and the club: the low-mid boom, the lowest octave =====
        if (! drops.empty())
            if (const auto* d = secOf (drops[0]); d != nullptr && d->heard())
            {
                const auto sh = shape (d->band);
                if (const auto& st = P.tone[(size_t) T::Drop][2]; st.valid())
                {
                    const auto over = sh[2] - st.median;
                    add (aTranslation, "In a car (the low-mid boom)", true, ramp (over, 2.0f, 5.0f), "the drop's 125 Hz band " + db (over) + " against the label",
                         "the low-mid boom dipped in the drops, the pads' boxiness cut (the autopilot)", d->startBar, suite::Fix::None, 0, false,
                         features ({ AP::mx2LowMidDropsF, AP::mx2BoxinessF }));
                }
                if (const auto& st = P.tone[(size_t) T::Drop][0]; st.valid())
                {
                    const auto off = sh[0] - st.median;
                    add (aTranslation, "In a club (the lowest octave)", true, ramp (std::abs (off), 3.0f, 6.0f), "the drop's 31 Hz band " + db (off) + " against the label",
                         off < 0.0f ? juce::String ("a sub under a thin bass, the sub held steady (the autopilot)") : juce::String ("a subsonic filter, the sub held steady (the autopilot)"), d->startBar,
                         suite::Fix::None, 0, false, off < 0.0f ? features ({ AP::subFillF, AP::mx2SubPerNoteF }) : features ({ AP::subsonicF, AP::mx2SubPerNoteF }));
                }
            }

        // ===== smooth section changes: a jump where the music should carry on =====
        if (songHeard)
        {
            float worst = 0.0f; int worstBar = -1;
            for (size_t i = 1; i < S.size(); ++i)
            {
                const auto& a = S[i - 1]; const auto& b = S[i];
                if (b.type == T::Drop || b.type == T::Breakdown || a.type == T::Build || a.lastDb < -90.0f || b.firstDb < -90.0f) continue;   // jumps there are the point
                if (std::abs (b.firstDb - a.lastDb) > std::abs (worst)) { worst = b.firstDb - a.lastDb; worstBar = b.startBar; }
            }
            add (aAR, "Smooth section changes", true, ramp (std::abs (worst), 4.0f, 8.0f),
                 worstBar > 0 ? "bar " + juce::String (worstBar) + ": " + db (worst) + " from one section to the next" : juce::String ("every change carries the level on"),
                 "levels ramp across the change, new parts eased in, the outro thinning gradually (the autopilot)", worstBar, suite::Fix::None, 0, false,
                 features ({ AP::mx2IntroEaseF, AP::mx2BreakdownEntryF, AP::mx2OutroThinF }));
        }

        // ===== fixes piling up on one track =====
        {
            const auto entries = suite::MixFixBus::all();
            const suite::MixFixBus::Entry* worst = nullptr;
            for (const auto& e : entries) if ((e.count > 25 || e.budgetHit) && (worst == nullptr || e.count > worst->count)) worst = &e;
            if (worst != nullptr)
                add (aBalance, "Fixes piling up", true, worst->budgetHit ? 0.6f : ramp ((float) worst->count, 25.0f, 40.0f),
                     worst->trackName() + ": " + juce::String (worst->count) + " mix fixes on" + (worst->budgetHit ? ", its level budget holding some back" : juce::String()),
                     "the budgets hold them (Tools > The mix fixes: switch some off for this track)", -1, suite::Fix::None, 0, false,
                     features ({ AP::mx2LevelBudgetF, AP::mx2BuildBudgetF }));
            else add (aBalance, "Fixes piling up", ! entries.empty(), 1.0f, entries.empty() ? juce::String ("no plugin reporting its mix fixes yet") : juce::String ("no track has too many fixes on"));
        }
    }
}
