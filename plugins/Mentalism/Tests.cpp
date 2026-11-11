// Offline tests for Mentalism. Usage: MentalismTests [output-dir]

#include "PluginProcessor.h"
#include "LikeMe.h"
#include "AutoReference.h"
#include "MixFix.h"
#include "Registry.h"
#include "PluginEditor.h"
#include "TestUtils.h"
#include "ConductorBus.h"
#include "StyleLog.h"
#include "SectionBus.h"
#include "SuiteUI.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<MentalismProcessor> make()
    {
        auto p = std::make_unique<MentalismProcessor>();
        prepare (*p);
        return p;
    }

    InputFn sine (float hz, float dbfs, float phase = 0.0f, bool invertRight = false)
    {
        const auto amp = juce::Decibels::decibelsToGain (dbfs);
        return [=] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) (pos + i) / sampleRate + phase);
                b.setSample (0, i, v);
                b.setSample (1, i, invertRight ? -v : v);
            }
        };
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    // Never write the real style log from tests.
    const auto testStyleLog = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SuiteTestsStyleLog.txt");
    suite::StyleLog::setFileForTests (testStyleLog);
    const auto testMySound = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SuiteTestsMySound.txt");
    testMySound.deleteFile();
    MySound::fileOverride() = testMySound;   // never the real My sound either
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Loudness (EBU Tech 3341 style)");
    {
        auto p = make();
        render (*p, 10.0, sine (1000.0f, -23.0f));
        const auto m = p->loudness.momentary.load(), s = p->loudness.shortTerm.load(), i = p->loudness.integrated();
        check (std::abs (m + 23.0f) < 0.2f && std::abs (s + 23.0f) < 0.2f && std::abs (i + 23.0f) < 0.2f,
               "1 kHz stereo sine at -23 dBFS reads -23 LUFS (M " + juce::String (m, 2) + ", S " + juce::String (s, 2)
               + ", I " + juce::String (i, 2) + ")");

        auto q = make();
        render (*q, 40.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int k = 0; k < b.getNumSamples(); ++k)
            {
                const auto t = (double) (pos + k) / sampleRate;
                const auto amp = juce::Decibels::decibelsToGain (t < 20.0 ? -20.0f : -60.0f);
                const auto v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t);
                b.setSample (0, k, v); b.setSample (1, k, v);
            }
        });
        const auto gated = q->loudness.integrated();
        check (std::abs (gated + 20.0f) < 0.3f, "relative gate ignores the quiet part (-20/-60 dB sections read " + juce::String (gated, 2) + " LUFS)");

        q->resetMeasurements();
        render (*q, 0.05, {});
        check (q->loudness.integrated() < -99.0f, "Reset clears integrated loudness");
    }

    section ("True peak");
    {
        auto p = make();
        // fs/4 sine at 45 degrees: every sample is at 0.707 but the waveform peaks at 1.0.
        render (*p, 1.0, sine ((float) sampleRate / 4.0f, 0.0f, juce::MathConstants<float>::pi / 4.0f));
        const auto tp = p->truePeak.peakDb();
        check (tp > -0.5f && tp < 0.3f, "finds the inter-sample peak (" + juce::String (tp, 2) + " dBTP; sample peak is -3.01)");
    }

    section ("Stereo");
    {
        auto p = make();
        render (*p, 1.0, sine (440.0f, -12.0f));
        check (p->stereo.correlation.load() > 0.99f && std::abs (p->stereo.monoLossDb.load()) < 0.1f, "mono signal: correlation +1, no mono loss");

        auto q = make();
        render (*q, 1.0, sine (440.0f, -12.0f, 0.0f, true));
        check (q->stereo.correlation.load() < -0.99f && q->stereo.monoLossDb.load() < -40.0f, "inverted channels: correlation -1, cancels in mono");

        auto w = make();
        render (*w, 1.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto mid = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 2000.0 * t);
                const auto side = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t);
                b.setSample (0, i, mid + side); b.setSample (1, i, mid - side);
            }
        });
        check (w->stereo.lowCorrelation.load() < 0.0f && w->stereo.correlation.load() > -0.2f,
               "wide bass under mono highs is caught by the low-end meter (" + juce::String (w->stereo.lowCorrelation.load(), 2) + ")");
    }

    section ("Tonal balance");
    {
        auto p = make();
        render (*p, 3.0, sine (1000.0f, -12.0f));
        int loudest = 0;
        for (int b = 1; b < analysis::numBands; ++b)
            if (p->bands.longDb[(size_t) b].load() > p->bands.longDb[(size_t) loudest].load()) loudest = b;
        check (loudest == 5, "a 1 kHz tone lands in the 1 kHz band");
        check (std::abs (p->bands.longSeconds.load() - 3.0f) < 0.15f, "counts 3 s of audible audio");

        auto q = make();
        render (*q, 1.0, {});
        check (q->bands.longSeconds.load() < 0.01f, "silence is not counted");
    }

    section ("Advice");
    {
        advisor::Measurement m;
        m.integratedLufs = -13.0f;
        m.truePeakDb = 0.4f;
        m.lowCorrelation = 0.3f;
        m.haveTonal = true;
        m.bandDiff[3] = 5.0f;   // 250 Hz heavy
        m.bandDiff[8] = -4.0f;  // 8 kHz light
        const auto advice = advisor::suggestions (m, -8.0f, -0.3f, "Club master");
        const auto text = advice.joinIntoString ("\n");
        std::cout << "    " << text.replace ("\n", "\n    ") << std::endl;
        check (text.contains ("5.0 dB quieter"), "flags loudness short of the target");
        check (text.contains ("True peak"), "flags true-peak overs");
        check (text.contains ("isn't mono"), "flags a wide low end");
        check (text.contains ("Low-mids (250 Hz) is 5.0 dB above") && text.contains ("mud"), "names the muddy band with a fix");
        check (text.indexOf ("250 Hz") < text.indexOf ("8 kHz"), "biggest tonal problem first");

        advisor::Measurement good;
        good.integratedLufs = -8.2f; good.truePeakDb = -0.5f; good.haveTonal = true;
        check (advisor::suggestions (good, -8.0f, -0.3f, "Club").joinIntoString ("") == "No big issues against this target.",
               "a balanced master gets a clean bill");

        const auto d = advisor::difference ({ 0, 10, 10, 10, 10, 10, 10, 10, 10, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, -10 });
        check (std::abs (d[5]) < 1.0e-4f, "overall level doesn't matter, only shape");
    }

    section ("Reference track");
    {
        // Write a -23 dBFS test file, analyse it, and keep it through save/restore.
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mentalism_ref_test.wav");
        juce::AudioBuffer<float> ref (2, (int) (sampleRate * 8.0));
        ref.clear();
        sine (1000.0f, -23.0f) (ref, 0);
        writeWav (file, ref);

        const auto profile = ReferenceProfile::analyse (file);
        check (profile.valid && std::abs (profile.lufs + 23.0f) < 0.2f, "reference analysed at " + juce::String (profile.lufs, 2) + " LUFS");

        auto p = make();
        p->setReference (profile);
        set (*p, Params::loudnessTarget, (float) advisor::matchReferenceIndex);
        check (std::abs (p->report().targetLufs + 23.0f) < 0.2f, "'Match reference track' uses the reference loudness");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        const auto restored = q->getReference();
        check (restored.valid && restored.name == profile.name && std::abs (restored.bands[5] - profile.bands[5]) < 0.01f,
               "reference survives save/restore (no need to reload the file)");
        file.deleteFile();
    }

    section ("Arrangement coach");
    {
        // 120 BPM, 2 s per bar: Groove 1-2, Breakdown 3-4, Build 5-6, Drop 7-8.
        suite::SectionMap map;
        map.totalBars = 8; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 3, suite::SectionType::Breakdown };
        map.sections[2] = { 5, suite::SectionType::Build };
        map.sections[3] = { 7, suite::SectionType::Drop };

        auto playSong = [&] (std::function<float (double)> levelDb)
        {
            suite::SectionBus::publish (map);
            auto p = make();
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            render (*p, 16.0, [levelDb] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto t = (double) (pos + i) / sampleRate;
                    const auto v = juce::Decibels::decibelsToGain (levelDb (t)) * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            }, nullptr, &head);
            p->setPlayHead (nullptr);
            return p->report();
        };

        // A flat, unexciting arrangement: breakdown barely below the drop, build doesn't grow.
        const auto bad = playSong ([] (double t) { return t < 4.0 ? -14.0f : t < 8.0 ? -15.0f : t < 12.0 ? -15.0f : -14.0f; });
        check (bad.arrangementAvailable && bad.sections[1].heard() && std::abs (bad.sections[1].lufs() + 15.0f) < 0.5f,
               "measures each section (breakdown " + juce::String (bad.sections[1].lufs(), 1) + " LUFS)");
        const auto badText = bad.arrangement.notes.joinIntoString ("\n");
        std::cout << "    " << badText.replace ("\n", "\n    ") << std::endl;
        check (badText.contains ("only 1.0 LU below the drop"), "flags a breakdown that's too loud");
        check (badText.contains ("Build (bars 5-6) only rises"), "flags a build that doesn't grow");
        check (bad.advice[0].startsWith ("Arrangement:"), "the top problem joins the main suggestions");
        {
            const auto& f = bad.arrangement.fixes;
            const auto has = [&f] (suite::Fix x) { return std::find (f.begin(), f.end(), x) != f.end(); };
            check (f.size() == (size_t) bad.arrangement.notes.size() && has (suite::Fix::BreakdownQuieter) && has (suite::Fix::BuildRise),
                   "each problem carries its fix (quieter breakdown, more rise)");
            bool confirmationsHaveNone = true;
            for (size_t i = (size_t) bad.arrangement.problems; i < f.size(); ++i) confirmationsHaveNone = confirmationsHaveNone && f[i] == suite::Fix::None;
            check (confirmationsHaveNone, "confirmations have no Fix button");
        }

        // A good arc: deep breakdown, rising build, loud drop.
        const auto good = playSong ([] (double t)
        {
            if (t < 4.0) return -16.0f;
            if (t < 8.0) return -22.0f;
            if (t < 12.0) return -22.0f + 8.0f * (float) ((t - 8.0) / 4.0);
            return -12.0f;
        });
        check (good.arrangement.problems == 0, "a good arc gets no complaints (" + good.arrangement.notes.joinIntoString (" | ") + ")");

        // Second drop weaker than the first.
        suite::SectionMap twoDrops;
        twoDrops.totalBars = 8; twoDrops.count = 3;
        twoDrops.sections[0] = { 1, suite::SectionType::Drop };
        twoDrops.sections[1] = { 4, suite::SectionType::Breakdown };
        twoDrops.sections[2] = { 6, suite::SectionType::Drop };
        std::vector<coach::SectionStat> stats (3);
        auto setLufs = [] (coach::SectionStat& st, float lufs) { st.blocks = 20; st.power = 20.0 * std::pow (10.0, (lufs + 0.691) / 10.0); };
        setLufs (stats[0], -9.0f); setLufs (stats[1], -16.0f); setLufs (stats[2], -11.0f);
        const auto r = coach::analyse (twoDrops, stats);
        check (r.notes.joinIntoString (" ").contains ("Drop 2 (bars 6-8) is 2.0 LU quieter than drop 1"), "flags a weaker second drop");
        check (std::find (r.fixes.begin(), r.fixes.end(), suite::Fix::LaterDropBigger) != r.fixes.end(), "... with a 'bigger later drops' fix");

        // After a fix, the fixed sections are forgotten so the coach hears them again.
        {
            suite::SectionBus::publish (map);
            auto p = make();
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            render (*p, 16.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto v = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) (pos + i) / sampleRate);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            }, nullptr, &head);
            p->setPlayHead (nullptr);
            p->forgetSections (suite::SectionType::Breakdown, map);
            const auto after = p->report();
            check (! after.sections[1].heard() && after.sections[3].heard(), "forgetting breakdowns keeps the other sections' measurements");
        }
        suite::SectionBus::withdraw();
    }

    section ("Pre-export checklist");
    {
        using checklist::State;
        MentalismProcessor::Report rep;
        rep.measuredSeconds = 240.0f;
        rep.targetLufs = -8.0f; rep.ceiling = -0.3f;
        rep.measurement.integratedLufs = -8.4f; rep.measurement.truePeakDb = -0.6f;
        rep.measurement.correlation = 0.6f; rep.measurement.lowCorrelation = 0.97f; rep.measurement.monoLossDb = 1.2f;
        auto items = checklist::check (rep, 0);
        bool allPass = true;
        for (auto& i : items) allPass = allPass && i.state == State::Pass;
        check (allPass && checklist::readyToExport (items), "a finished master passes every item");

        auto stateOf = [] (const std::vector<checklist::Item>& list, const char* name)
        {
            for (auto& i : list) if (i.name == name) return i.state;
            return State::Unknown;
        };
        rep.measurement.truePeakDb = 0.4f;
        rep.measurement.lowCorrelation = 0.5f;
        rep.measurement.integratedLufs = -11.5f;
        items = checklist::check (rep, 37);
        check (stateOf (items, "True peak") == State::Fail && stateOf (items, "Clipping") == State::Fail
                   && stateOf (items, "Low end in mono") == State::Fail && stateOf (items, "Loudness") == State::Fail
                   && ! checklist::readyToExport (items), "over the ceiling, clipped, wide bass and too quiet all fail");

        // A breakdown that's nearly silent.
        rep = {};
        rep.measuredSeconds = 60.0f;
        rep.measurement.integratedLufs = -8.0f; rep.targetLufs = -8.0f; rep.measurement.truePeakDb = -1.0f; rep.ceiling = -0.3f;
        rep.arrangementAvailable = true;
        rep.map.totalBars = 8; rep.map.count = 2;
        rep.map.sections[0] = { 1, suite::SectionType::Drop };
        rep.map.sections[1] = { 5, suite::SectionType::Breakdown };
        rep.sections.resize (2);
        auto setLufs = [] (coach::SectionStat& st, float lufs) { st.blocks = 20; st.power = 20.0 * std::pow (10.0, (lufs + 0.691) / 10.0); };
        setLufs (rep.sections[0], -8.0f); setLufs (rep.sections[1], -52.0f);
        rep.arrangement = coach::analyse (rep.map, rep.sections);
        items = checklist::check (rep, 0);
        juce::String detail;
        for (auto& i : items) if (i.name == "No silent sections") detail = i.detail;
        check (stateOf (items, "No silent sections") == State::Fail && detail.contains ("Breakdown (bars 5-8)"), "flags a silent breakdown: " + detail);

        // DJ-friendly intro and outro, from the Prima Materia tracks in the suite.
        {
            using namespace suite;
            const auto introBit = 1u << (int) SectionType::Intro, dropBit = 1u << (int) SectionType::Drop;
            std::vector<SuiteEntry> tracks (3);
            tracks[0].plugin = "Prima Materia"; tracks[0].track = "Kick"; tracks[0].flags = Status::roleSet; tracks[0].metrics = { 1.0f, 0, 0, (float) (introBit | dropBit) };
            tracks[1].plugin = "Prima Materia"; tracks[1].track = "Pad";  tracks[1].flags = Status::roleSet; tracks[1].metrics = { 4.0f, 0, 0, (float) (introBit | dropBit) };
            tracks[2].plugin = "Prima Materia"; tracks[2].track = "Lead"; tracks[2].flags = Status::roleSet; tracks[2].metrics = { 5.0f, 0, 0, (float) dropBit };
            auto dj = checklist::djCheck (tracks, true);
            check (dj.state == State::Warn && dj.detail.contains ("Pad") && ! dj.detail.contains ("Lead") && dj.fix == Fix::DjIntroOutro,
                   "flags a pad in the intro, offering a fix: " + dj.detail);
            tracks[1].metrics[3] = (float) dropBit;
            check (checklist::djCheck (tracks, true).state == State::Pass, "beat-led intro and outro pass");
            tracks[0].metrics[3] = (float) dropBit;
            check (checklist::djCheck (tracks, true).detail.contains ("No kick in the intro"), "no kick in the intro is flagged");
            check (checklist::djCheck ({}, true).optional && checklist::djCheck ({}, true).state == State::Unknown, "without Prima Materia it can't tell (and doesn't block export)");
        }

        rep = {};
        check (stateOf (checklist::check (rep, 0), "Measured the song") == State::Unknown && ! checklist::readyToExport (checklist::check (rep, 0)),
               "nothing measured: not ready");

        auto p = make();
        render (*p, 0.2, [] (juce::AudioBuffer<float>& b, int64_t) { for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, i % 50 < 25 ? 1.0f : -1.0f); b.setSample (1, i, 0.0f); } });
        check (p->clippedSamples.load() > 1000, "counts samples at full scale (" + juce::String (p->clippedSamples.load()) + ")");
        if (outDir != juce::File())
        {
            render (*p, 3.0, sine (440.0f, -10.0f));
            ChecklistPanel panel (*p);
            snapshot (panel, outDir.getChildFile ("mentalism_checklist.png"));
        }
    }

    section ("New song (session preset)");
    {
        auto p = make();
        suite::SessionBus::send ({ 1, 9, 0 });   // Uplifting radio edit: streaming target
        p->pollSession();
        check ((int) p->apvts.getRawParameterValue (Params::loudnessTarget)->load() == suite::sessionStyles()[1].loudnessTarget,
               "sets the style's loudness target (streaming for a radio edit)");
    }

    section ("Masking map");
    {
        using namespace suite;
        auto track = [] (uint32_t id, const char* name, int role, std::array<float, 10> bands)
        {
            SuiteEntry e; e.id = id; e.plugin = "Prima Materia"; e.track = name; e.flags = Status::roleSet | Status::hasBands;
            e.metrics = { (float) role, -12.0f, 0.0f, 0.0f }; e.bands = bands; return e;
        };
        //                                       31   63  125  250  500   1k   2k   4k   8k  16k
        const auto pad  = track (11, "Pad",  4, { -60, -50, -30, -22, -20, -24, -18, -30, -40, -60 });
        const auto lead = track (12, "Lead", 5, { -70, -60, -45, -35, -28, -22, -16, -20, -30, -45 });
        const auto hats = track (13, "Hats", 3, { -80, -80, -70, -60, -50, -40, -30, -20, -14, -30 });
        const auto found = masking::find ({ pad, lead, hats });
        check (! found.empty() && found[0].band == 6 && found[0].carveId == 11 && found[0].text.contains ("Pad and Lead clash at 2 kHz"),
               "finds the pad and lead fighting at 2 kHz and carves the pad: " + (found.empty() ? juce::String ("-") : found[0].text));
        bool hatsVsPad = false;
        for (auto& c : found) if (c.text.contains ("Hats") && c.text.contains ("Pad")) hatsVsPad = true;
        check (! hatsVsPad, "tracks whose main energy is elsewhere don't clash");
        check (masking::priority (6) > masking::priority (5) && masking::priority (5) > masking::priority (4) && masking::priority (4) > masking::priority (7),
               "vocal keeps over lead, lead over pads, pads over FX");
        check (masking::find ({ pad }).empty(), "one track: nothing to compare");
        if (outDir != juce::File())
        {
            Registry::reset();
            for (auto& e : { pad, lead, hats })
                Registry::announce (e.id, e.plugin, e.track, e.flags, e.metrics, e.bands);
            auto m = make();
            std::unique_ptr<juce::AudioProcessorEditor> ed (m->createEditor());
            if (auto* tabs = dynamic_cast<suite::ui::TabBar*> (ed->findChildWithID ("tabs"))) tabs->setSelected (2);
            if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) juce::ignoreUnused (t);
            snapshot (*ed, outDir.getChildFile ("mentalism_masking.png"));
            ed.reset();
            Registry::reset();
        }
    }

    section ("Match reference");
    {
        suite::FixBus::reset();
        auto p = make();
        check (p->matchReference() == 0, "nothing to match without a reference");
        juce::Random rng (5);
        render (*p, 12.0, [&rng] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int i = 0; i < b.getNumSamples(); ++i) { const auto v = 0.2f * (rng.nextFloat() * 2.0f - 1.0f); b.setSample (0, i, v); b.setSample (1, i, v); }
        });
        ReferenceProfile ref;
        ref.valid = true; ref.name = "Brighter, leaner"; ref.lufs = -8.0f;
        for (size_t b = 0; b < (size_t) analysis::numBands; ++b) ref.bands[b] = p->bands.longDb[b].load();
        for (size_t b : { (size_t) 0, (size_t) 1, (size_t) 2 }) ref.bands[b] -= 2.0f;   // the reference has less low end
        for (size_t b : { (size_t) 8, (size_t) 9 }) ref.bands[b] += 4.0f;               // ... and more top
        p->setReference (ref);
        const auto seq = p->matchReference();
        const auto lo = p->lastMatchLow.load(), hi = p->lastMatchHigh.load();
        check (seq != 0 && lo < -0.5f && lo > -1.5f && hi > 1.5f && hi < 2.5f,
               "moves half-way: less low, more top (" + juce::String (lo, 2) + " / +" + juce::String (hi, 2) + " dB)");
        check ((int) p->apvts.getRawParameterValue (Params::loudnessTarget)->load() == advisor::matchReferenceIndex, "and aims the loudness at the reference");
        float l = 0, h = 0;
        suite::FixBus::Message m;
        check (suite::FixBus::read (seq, m) && m.fix == suite::Fix::ReferenceTone && (suite::unpackTone (m.target, l, h), std::abs (l - lo) < 0.01f && std::abs (h - hi) < 0.01f),
               "the amounts travel with the fix");
        check (p->getFixHistory().back().fix == suite::Fix::ReferenceTone, "and it's in the history, to undo");
    }

    section ("Versions");
    {
        auto p = make();
        p->applyVersion (suite::radioEdit);
        const auto radio = (int) p->apvts.getRawParameterValue (Params::loudnessTarget)->load();
        p->applyVersion (suite::extendedMix);
        check (radio == 2 && (int) p->apvts.getRawParameterValue (Params::loudnessTarget)->load() == 0, "radio edit: streaming target; extended mix: club");
    }

    section ("Fix history");
    {
        suite::FixBus::reset();
        auto p = make();
        const auto a = p->sendFix (suite::Fix::BreakdownQuieter);
        const auto b = p->sendFix (suite::Fix::RoughMix);
        suite::FixBus::acknowledge (a);   // as if one plugin applied each
        suite::FixBus::acknowledge (b);
        const auto h = p->getFixHistory();
        check (h.size() == 2 && h[0].seq == a && h[1].fix == suite::Fix::RoughMix, "remembers every fix sent, in order");
        const auto u = p->undoFix (a);
        check (u != 0 && p->getFixHistory()[0].undoSeq == u, "undo any of them, not just the last");
        check (p->undoFix (a) == 0, "a fix can only be undone once");
        if (outDir != juce::File())
        {
            FixHistoryPanel panel (*p);
            snapshot (panel, outDir.getChildFile ("mentalism_history.png"));
        }
    }

    section ("Arc match, auto-coach and Finish the song");
    {
        // Arc match: a breakdown drawn at 40% should sit 7.2 LU under the drop.
        suite::SectionMap arc;
        arc.totalBars = 8; arc.count = 2;
        arc.sections[0] = { 1, suite::SectionType::Drop };
        arc.sections[1] = { 5, suite::SectionType::Breakdown, 40 };
        std::vector<coach::SectionStat> stats (2);
        auto setLufs = [] (coach::SectionStat& st, float lufs) { st.blocks = 20; st.power = 20.0 * std::pow (10.0, (lufs + 0.691) / 10.0); };
        setLufs (stats[0], -8.0f); setLufs (stats[1], -10.0f);
        const auto loud = coach::analyse (arc, stats);
        const auto text = loud.notes.joinIntoString (" | ");
        int index = -1; float db = 0.0f;
        if (loud.problems > 0) suite::unpackArc (loud.fixData[0], index, db);
        check (loud.problems == 1 && text.contains ("5.2 LU louder than your energy curve") && loud.fixes[0] == suite::Fix::ArcMatch
                   && index == 1 && std::abs (db + 5.0f) < 0.01f,
               "a breakdown 5.2 LU above its drawn energy gets an arc fix for that section, -5 dB (" + text + ")");
        check (! text.contains ("below the drop"), "the drawn curve replaces the usual breakdown contrast rule");
        setLufs (stats[1], -15.0f);
        check (coach::analyse (arc, stats).problems == 0 && coach::analyse (arc, stats).notes.joinIntoString (" ").contains ("follows your energy curve"),
               "within 3 LU of the curve: no complaint");

        // Auto-coach: after a complete play-through of a flat arrangement, one pass of fixes.
        suite::SectionMap map;
        map.totalBars = 8; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 3, suite::SectionType::Breakdown };
        map.sections[2] = { 5, suite::SectionType::Build };
        map.sections[3] = { 7, suite::SectionType::Drop };
        suite::SectionBus::publish (map);
        auto flat = [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) (pos + i) / sampleRate);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
        auto play = [&] (MentalismProcessor& m)
        {
            FakePlayHead head;
            head.bpm = 120.0;
            m.setPlayHead (&head);
            render (m, 16.0, flat, nullptr, &head);
            m.setPlayHead (nullptr);
        };
        suite::FixBus::reset();
        auto p = make();
        set (*p, Params::problemWatch, 0.0f);   // (the watcher keeps section loudness by itself when the coach is off)
        play (*p);
        const auto before = p->report();
        p->runAutomation();
        check (suite::FixBus::latest() == 0, "auto-coach off: nothing is sent");
        set (*p, Params::autoCoach, 1.0f);
        p->runAutomation();
        const auto sent = (int) suite::FixBus::latest();
        check (before.arrangement.problems >= 2 && sent >= 2 && p->autoCoachPasses() == 1,
               "auto-coach on: one pass sends the coach's fixes (" + juce::String (sent) + " for " + juce::String (before.arrangement.problems) + " notes)");
        check (! p->report().sections[1].heard() && p->autoCoachStatus().contains ("Play the song again"), "and waits to hear the fixed sections again");
        p->runAutomation();
        check ((int) suite::FixBus::latest() == sent, "nothing more until they've been heard");

        // Finish the song: gain staging now, the rough mix once the drop is heard again.
        suite::FixBus::reset();
        auto q = make();
        play (*q);
        const auto lines = q->finishSong();
        suite::FixBus::Message first;
        check (suite::FixBus::read (1, first) && first.fix == suite::Fix::GainStage && q->finishStage() == MentalismProcessor::FinishStage::WaitingForDrop
                   && q->apvts.getRawParameterValue (Params::autoCoach)->load() > 0.5f,
               "Finish: gain staging first, auto-coach on, waiting for the drop (" + lines.joinIntoString (" / ") + ")");
        q->runAutomation();
        check (q->finishStage() == MentalismProcessor::FinishStage::WaitingForDrop, "the drop hasn't been heard again yet");
        play (*q);
        const auto beforeRough = suite::FixBus::latest();
        q->runAutomation();
        suite::FixBus::Message rough;
        check (suite::FixBus::read (beforeRough + 1, rough) && rough.fix == suite::Fix::RoughMix && q->finishStage() == MentalismProcessor::FinishStage::Coaching,
               "after the drop: the rough mix, then coaching");
        check (! q->report().arrangement.complete && q->finishStatus().contains ("Play the whole song"), "the coach starts from a fresh listen");
        suite::SectionBus::withdraw();
    }

    section ("My sound (from your tracks)");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MentalismMySound");
        dir.deleteRecursively();
        dir.createDirectory();
        juce::Array<juce::File> tracks;
        for (const auto [name, db] : { std::pair<const char*, float> { "one.wav", -10.0f }, { "two.wav", -14.0f } })
        {
            juce::AudioBuffer<float> b (2, (int) (sampleRate * 6.0));
            b.clear();
            sine (1000.0f, db) (b, 0);
            const auto f = dir.getChildFile (name);
            writeWav (f, b);
            tracks.add (f);
        }
        const auto mine = MentalismProcessor::analyseMySound (tracks);
        check (mine.valid && mine.tracks == 2 && std::abs (mine.lufs + 12.0f) < 0.3f,
               "learns from two tracks: their average loudness (" + juce::String (mine.lufs, 2) + " LUFS)");
        const auto back = MySound::fromText (mine.toText());
        check (back.valid && back.tracks == 2 && std::abs (back.lufs - mine.lufs) < 0.01f && std::abs (back.bands[5] - mine.bands[5]) < 0.01f,
               "and keeps it as a file for every project");

        auto p = make();
        set (*p, Params::loudnessTarget, (float) advisor::mySoundIndex);
        set (*p, Params::tonalTarget, (float) advisor::mySoundTonalIndex);
        p->setMySound ({});
        auto r = p->report();
        check (std::abs (r.targetLufs + 8.0f) < 0.01f && ! r.tonalAvailable, "before learning: the club target, and no tonal target yet");
        p->setMySound (mine);
        r = p->report();
        check (std::abs (r.targetLufs - mine.lufs) < 0.01f && r.targetName.contains ("your sound") && r.tonalAvailable
                   && std::abs (r.ceiling - mine.ceiling()) < 0.01f,
               "'My sound' targets your loudness, ceiling and tonal balance (" + r.targetName + ")");
        dir.deleteRecursively();
    }

    section ("DJ phrasing and the masking watch");
    {
        MentalismProcessor::Report rep {};
        rep.arrangementAvailable = true;
        rep.map.totalBars = 100; rep.map.count = 3;
        rep.map.sections[0] = { 1, suite::SectionType::Intro };
        rep.map.sections[1] = { 21, suite::SectionType::Drop };
        rep.map.sections[2] = { 85, suite::SectionType::Outro };
        auto find = [] (const std::vector<checklist::Item>& items)
        {
            for (const auto& i : items) if (i.name == "DJ phrasing") return i;
            return checklist::Item {};
        };
        const auto off = find (checklist::check (rep, 0));
        check (off.state == checklist::State::Warn && off.fix == suite::Fix::DjPhrasing && off.detail.contains ("intro is 20 bars"),
               "the checklist flags a 20-bar intro with a fix (" + off.detail + ")");
        rep.map = suite::dj::fixPhrasing (rep.map);
        check (find (checklist::check (rep, 0)).state == checklist::State::Pass, "and passes once it's on the phrase grid");

        auto p = make();
        masking::Clash c;
        c.carveTrack = "Pad"; c.keepTrack = "Lead"; c.band = 6; c.text = "Pad and Lead clash at 2 kHz";
        p->watchMasking ({ c });
        p->watchMasking ({ c });
        check (p->maskingSuggestions().isEmpty(), "a clash isn't suggested after two checks (a minute)");
        p->watchMasking ({ c });
        check (p->maskingSuggestions().size() == 1 && p->report().advice.joinIntoString ("|").contains ("Masking: Pad and Lead clash at 2 kHz"),
               "after three checks in a row it joins the suggestions");
        p->watchMasking ({});
        check (p->maskingSuggestions().isEmpty(), "and leaves when the clash is gone (nothing is changed automatically)");
    }

    section ("Build brightness, pass compare and the mix report");
    {
        suite::SectionMap map;
        map.totalBars = 8; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Build };
        map.sections[1] = { 5, suite::SectionType::Drop };
        std::vector<coach::SectionStat> stats (2);
        auto setLufs = [] (coach::SectionStat& st, float lufs) { st.blocks = 20; st.power = 20.0 * std::pow (10.0, (lufs + 0.691) / 10.0); };
        setLufs (stats[0], -14.0f); setLufs (stats[1], -9.0f);
        auto& build = stats[0];
        build.firstBlocks = 10; build.secondBlocks = 10;
        build.firstHalf = 10.0 * std::pow (10.0, (-16.0 + 0.691) / 10.0); build.secondHalf = 10.0 * std::pow (10.0, (-12.0 + 0.691) / 10.0);   // rises 4 LU
        build.brightFirst = 10.0 * -10.0; build.brightSecond = 10.0 * -9.5;   // but hardly brighter (+0.5 dB)
        const auto dull = coach::analyse (map, stats);
        check (dull.problems == 1 && dull.notes[0].contains ("gets louder but not brighter") && dull.fixes[0] == suite::Fix::BuildRise,
               "a build that rises in level but not in top end is flagged (" + dull.notes[0] + ")");
        build.brightSecond = 10.0 * -4.0;   // +6 dB of top end
        check (coach::analyse (map, stats).problems == 0, "one that opens up passes");
        build.brightFirst = 10.0 * -80.0; build.brightSecond = 10.0 * -80.0;   // no top end at all: nothing to judge
        check (coach::analyse (map, stats).problems == 0, "a build with no top end isn't judged on brightness");

        MentalismProcessor::Pass before, now;
        before.valid = now.valid = true;
        before.lufs = -10.0f; now.lufs = -8.8f;
        for (size_t b = 0; b < before.bands.size(); ++b) before.bands[b] = now.bands[b] = -30.0f;
        now.bands[6] = -27.0f;   // 2 kHz up 3 dB
        const auto text = MentalismProcessor::comparePasses (before, now);
        check (text.contains ("1.2 LU louder") && text.contains ("turn the master down 1.2 dB") && text.contains ("2 kHz +"),
               "pass compare: how much louder, how to match it, and what changed in tone (" + text + ")");

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MentalismReports");
        dir.deleteRecursively();
        MentalismProcessor::reportFolderOverride() = dir;
        auto p = make();
        render (*p, 3.0, sine (1000.0f, -14.0f));
        const auto file = p->saveMixReport();
        const auto report = file.loadFileAsString();
        check (file.existsAsFile() && report.contains ("MIX REPORT") && report.contains ("Loudness:") && report.contains ("CHECKLIST") && report.contains ("True peak"),
               "the mix report is saved as text (" + file.getFileName() + ")");
        MentalismProcessor::reportFolderOverride() = juce::File();
        dir.deleteRecursively();
    }

    section ("Your own coach numbers");
    {
        const auto t = coach::learnThresholds ({ 10.0f, 9.0f, 11.0f }, { 4.0f, 3.0f, 5.0f });
        check (t.personal && std::abs (t.minContrast - 7.0f) < 0.01f && std::abs (t.maxContrast - 13.0f) < 0.01f && std::abs (t.minRise - 2.0f) < 0.01f,
               "from three finished songs: breakdowns 7-13 LU under the drop, builds rising 2 LU or more");
        check (! coach::learnThresholds ({ 10.0f, 9.0f }, {}).personal, "two songs aren't enough");
        suite::SectionMap map;
        map.totalBars = 8; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Breakdown };
        map.sections[1] = { 5, suite::SectionType::Drop };
        std::vector<coach::SectionStat> stats (2);
        auto setLufs = [] (coach::SectionStat& st, float lufs) { st.blocks = 20; st.power = 20.0 * std::pow (10.0, (lufs + 0.691) / 10.0); };
        setLufs (stats[0], -15.0f); setLufs (stats[1], -9.0f);   // 6 LU: fine by the generic rule, shallow for you
        check (coach::analyse (map, stats).problems == 0, "a 6 LU breakdown passes the generic rule");
        const auto mine = coach::analyse (map, stats, t);
        check (mine.problems == 1 && mine.notes[0].contains ("your usual: 10 LU"), "but not yours (" + mine.notes[0] + ")");
    }

    section ("Sections too short for their job");
    {
        suite::SectionMap song;
        song.totalBars = 96; song.count = 4;
        song.sections[0] = { 1, suite::SectionType::Intro };
        song.sections[1] = { 33, suite::SectionType::Breakdown };   // 8 bars: too short
        song.sections[2] = { 41, suite::SectionType::Build };       // 4 bars: too short
        song.sections[3] = { 45, suite::SectionType::Drop };
        std::vector<coach::SectionStat> none (4);
        const auto r = coach::analyse (song, none);
        int index = -1; float bars = 0.0f;
        if (r.problems >= 2) suite::unpackArc (r.fixData[1], index, bars);
        check (r.problems == 2 && r.notes[0].contains ("only 8 bars") && r.notes[1].contains ("a build needs 8 or more")
                   && r.fixes[1] == suite::Fix::ExtendSection && index == 2 && std::abs (bars - 8.0f) < 0.01f,
               "an 8-bar breakdown and a 4-bar build are flagged, with a fix to lengthen each in Ouroboros (" + r.notes.joinIntoString (" | ") + ")");
        suite::SectionMap loop = song;
        loop.totalBars = 32;
        check (coach::analyse (loop, none).problems == 0, "a 32-bar sketch isn't judged on lengths");
    }

    section ("Mix DNA and 'sounds like me'");
    {
        testStyleLog.deleteFile();
        auto shape = [] (float top)
        {
            juce::StringArray b;
            for (int i = 0; i < analysis::numBands; ++i) b.add (juce::String (-20.0f - 2.0f * (float) i + (i >= 8 ? top : 0.0f), 2));
            return b.joinIntoString (",");
        };
        suite::StyleLog::add ("master", { { "lufs", "-8.0" }, { "truepeak", "-0.3" }, { "bands", shape (0.0f) } });
        check (! MySound::fromFinishedSongs (suite::StyleLog::read()).valid, "one finished master isn't enough");
        suite::StyleLog::add ("master", { { "lufs", "-9.0" }, { "truepeak", "-0.5" }, { "bands", shape (1.0f) } });
        suite::StyleLog::add ("master", { { "lufs", "-8.4" }, { "truepeak", "-0.4" }, { "bands", shape (0.4f) } });
        suite::StyleLog::add ("master", { { "lufs", "-7.0" }, { "truepeak", "-0.2" } });   // an older entry with no tonal balance
        const auto dna = MySound::fromFinishedSongs (suite::StyleLog::read());
        check (dna.valid && dna.tracks == 3 && std::abs (dna.lufs + 8.4f) < 0.01f && std::abs (dna.bands[9] - (-38.0f + 0.4f)) < 0.01f && dna.learnedFromSongs(),
               "three finished masters teach your sound: the median loudness (" + juce::String (dna.lufs, 1) + " LUFS) and balance");

        auto p = make();
        p->setMySound ({});
        check (p->learnMixDna() && p->getMySound().learnedFromSongs() && MySound::load (testMySound).valid, "My sound learns it by itself, and keeps it");
        MySound chosen = dna;
        chosen.names = { "my track.wav" };
        p->setMySound (chosen);
        check (! p->learnMixDna() && ! p->getMySound().learnedFromSongs(), "but never replaces tracks you chose yourself");
        p->setMySound (dna);
        suite::SessionStyle style;
        p->applySession (style, 9, 0);
        check ((int) p->apvts.getRawParameterValue (Params::tonalTarget)->load() == advisor::mySoundTonalIndex, "a new song aims for your tonal balance");

        suite::StyleLog::add ("song", { { "key", "Am" }, { "bpm", "138" }, { "bars", "224" }, { "structure", "Intro-Breakdown-Drop-Outro" } });
        suite::StyleLog::add ("song", { { "key", "Am" }, { "bpm", "140" }, { "bars", "232" }, { "structure", "Intro-Breakdown-Drop-Outro" } });
        suite::StyleLog::add ("song", { { "key", "Em" }, { "bpm", "138" }, { "bars", "224" }, { "structure", "Intro-Drop-Outro" } });
        likeme::Song mine;
        mine.key = "Am"; mine.bpm = 138.0f; mine.structure = "Intro-Breakdown-Drop-Outro"; mine.bars = 224; mine.lufs = -8.5f;
        mine.haveBands = true;
        for (size_t b = 0; b < mine.bands.size(); ++b) mine.bands[b] = dna.bands[b] - 3.0f;   // same shape, quieter
        likeme::Song other = mine;
        other.key = "F"; other.bpm = 128.0f; other.structure = "Drop-Breakdown-Drop"; other.bars = 128; other.lufs = -13.0f;
        other.bands[9] += 6.0f;
        const auto a = likeme::compare (suite::StyleLog::read(), mine, dna), b = likeme::compare (suite::StyleLog::read(), other, dna);
        check (a.enough && a.score >= 90, "a song made your usual way scores high (" + juce::String (a.score) + "%)");
        check (b.score <= 35 && b.text().contains ("rarely write in major") && b.text().contains ("10 slower") && b.text().contains ("new for you")
                   && b.text().contains ("more at 16 kHz"),
               "a song that leaves your sound scores low and says where (" + juce::String (b.score) + "%)");
        testStyleLog.deleteFile();
        check (! likeme::compare (suite::StyleLog::read(), mine, dna).enough, "with no history it says what it needs");
    }

    section ("Live mix checks and A/B");
    {
        suite::Registry::reset();
        auto p = make();
        check (p->liveChecks().isEmpty(), "nothing to say about a quiet suite");
        suite::Registry::announce (9001, "Prima Materia", "Lead", suite::Status::clipping, {});
        suite::Registry::announce (9002, "Solve et Coagula", "Vox", suite::Status::vocalBuried, {});
        const auto live = p->liveChecks().joinIntoString ("\n");
        check (live.contains ("'Lead' is going over 0 dBFS") && live.contains ("vocal on 'Vox' has been buried"), "a clipping track and a buried vocal are flagged");
        check (p->report().advice[0].startsWith ("Live:"), "live problems come first in the suggestions");
        suite::Registry::reset();

        // A drop that's mono for 8 s.
        suite::SectionMap m;
        m.totalBars = 16; m.count = 1;
        m.sections[0] = { 1, suite::SectionType::Drop };
        suite::SectionBus::publish (m);
        auto q = make();
        FakePlayHead head;
        q->setPlayHead (&head);
        juce::Random rng (3);
        auto noise = [&rng] (bool mono)
        {
            return [&rng, mono] (juce::AudioBuffer<float>& b, int64_t)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto a = 0.2f * (rng.nextFloat() * 2.0f - 1.0f), c = 0.2f * (rng.nextFloat() * 2.0f - 1.0f);
                    b.setSample (0, i, a); b.setSample (1, i, mono ? a : c);
                }
            };
        };
        render (*q, 6.0, noise (true), nullptr, &head);
        check (q->liveChecks().isEmpty(), "6 s of a mono drop: not yet");
        render (*q, 6.0, noise (true), nullptr, &head);   // counted once the drop is loud enough (short-term loudness takes ~3 s)
        check (q->liveChecks().joinIntoString ("").contains ("drop is almost mono"), "after 8 s: the drop is almost mono");
        render (*q, 2.0, noise (false), nullptr, &head);
        check (q->liveChecks().isEmpty(), "and it clears when the drop opens up");
        q->setPlayHead (nullptr);
        suite::SectionBus::withdraw();

        // A/B: a reference whose loudest part is a 1 kHz tone, heard at the mix's loudness.
        const auto refFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MentalismABRef.wav");
        {
            juce::AudioBuffer<float> b (2, (int) (sampleRate * 40.0));
            b.clear();
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) i / sampleRate;
                const auto loud = t >= 10.0 && t < 30.0;
                const auto v = (float) ((loud ? 0.25 : 0.02) * std::sin (juce::MathConstants<double>::twoPi * (loud ? 1000.0 : 440.0) * t));
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            writeWav (refFile, b);
        }
        const auto ref = ReferenceProfile::analyse (refFile);
        check (ref.valid && ref.excerpt != nullptr && ref.excerpt->audio.getNumSamples() == (int) (20.0 * sampleRate),
               "loading a reference keeps its loudest 20 s for A/B (" + juce::String (ref.excerpt != nullptr ? ref.excerpt->lufs : -100.0f, 1) + " LUFS)");
        auto r = make();
        r->setReference (ref);
        juce::Random rng2 (9);
        auto mix = [&rng2] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int i = 0; i < b.getNumSamples(); ++i) { const auto v = 0.05f * (rng2.nextFloat() * 2.0f - 1.0f); b.setSample (0, i, v); b.setSample (1, i, v); }
        };
        render (*r, 4.0, mix);
        const auto mixLufs = r->report().shortTerm;
        set (*r, Params::compareAB, 1.0f);
        juce::AudioBuffer<float> heard;
        render (*r, 4.0, mix, &heard);
        analysis::LoudnessMeter lm;
        lm.prepare (sampleRate);
        for (int i = (int) sampleRate; i < heard.getNumSamples(); ++i) lm.process (heard.getSample (0, i), heard.getSample (1, i));
        auto toneLevel = [&heard] (double hz)
        {
            double c = 0.0, sn = 0.0;
            const auto i0 = (int) sampleRate, i1 = heard.getNumSamples();
            for (int i = i0; i < i1; ++i)
            {
                const auto w = juce::MathConstants<double>::twoPi * hz * i / sampleRate;
                c += heard.getSample (0, i) * std::cos (w); sn += heard.getSample (0, i) * std::sin (w);
            }
            return 2.0 * std::sqrt (c * c + sn * sn) / (i1 - i0);
        };
        check (toneLevel (1000.0) > 0.01 && toneLevel (1000.0) > 20.0 * toneLevel (440.0) && std::abs (lm.integrated() - mixLufs) < 1.0f,
               "A/B plays the reference's loud part at the mix's loudness (" + juce::String (lm.integrated(), 1) + " vs " + juce::String (mixLufs, 1) + " LUFS)");
        set (*r, Params::compareAB, 0.0f);
        juce::AudioBuffer<float> back;
        render (*r, 0.5, mix, &back);
        check (! r->abPlaying.load() && peakBetween (back, 0.1, 0.5) < 0.06f, "and switching it off brings the mix back");
        refFile.deleteFile();
    }

    section ("Auto-reference from a folder");
    {
        check (autoref::keyFromName ("Artist - Track (F#m, 138)") == std::make_pair (6, true) && autoref::keyFromName ("Song in Bb major") == std::make_pair (10, false)
                   && ! autoref::keyFromName ("Breakdown Mix").has_value(),
               "keys in file names: F#m, Bb major (and not 'Breakdown')");
        check (autoref::bpmFromName ("Track (Am, 138)") == 138.0f && autoref::bpmFromName ("140bpm Anthem") == 140.0f && autoref::bpmFromName ("Mix 2") == 0.0f,
               "tempos in file names");
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MentalismAutoRef");
        dir.deleteRecursively();
        dir.createDirectory();
        auto write = [&dir] (const juce::String& name, float hz)
        {
            juce::AudioBuffer<float> b (2, (int) (sampleRate * 6.0));
            juce::Random rng (2);
            float lp = 0.0f;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                lp += 0.05f * (rng.nextFloat() * 2.0f - 1.0f - lp);
                const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate) + 0.5f * lp;
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            writeWav (dir.getChildFile (name + ".wav"), b);
        };
        write ("Bright one (Am, 138)", 6000.0f);
        write ("Dark one (Am, 138)", 120.0f);
        write ("Dark one (C#m, 128)", 120.0f);
        // The mix so far: dark (a low tone over the same noise).
        advisor::Bands mix {};
        {
            const auto p = ReferenceProfile::analyse (dir.getChildFile ("Dark one (C#m, 128).wav"));
            mix = p.bands;
        }
        const auto [pick, why] = MentalismProcessor::chooseReference (dir, mix, true, 9, true, 138.0f);
        check (pick.valid && pick.name == "Dark one (Am, 138)", "picks the track with the closest tone, key and tempo (" + why + ")");
        const auto [noTone, why2] = MentalismProcessor::chooseReference (dir, mix, false, 9, true, 138.0f);
        check (noTone.name.contains ("(Am, 138)"), "without a mix yet, the names' key and tempo decide (" + why2 + ")");
        dir.deleteRecursively();
    }

    section ("Fixes that check themselves");
    {
        suite::FixBus::reset();
        auto p = make();
        const auto quieter = p->sendFix (suite::Fix::BreakdownQuieter), rise = p->sendFix (suite::Fix::BuildRise);
        p->lastApplied = { { suite::Fix::BreakdownQuieter, 0u, quieter }, { suite::Fix::BuildRise, 0u, rise } };
        p->problemsBeforeLastPass = 2;
        coach::Result now;   // the next pass: the breakdown is still too close, the build is fine now, something else came up
        now.problems = 2;
        now.fixes = { suite::Fix::BreakdownQuieter, suite::Fix::IntroQuieter };
        now.fixData = { 0u, 0u };
        now.notes = { "breakdown", "intro" };
        const auto verdict = p->verifyLastPass (now).joinIntoString ("; ");
        uint32_t undoneQuieter = 0, undoneRise = 0;
        for (const auto& r : p->getFixHistory()) { if (r.seq == quieter) undoneQuieter = r.undoSeq; if (r.seq == rise) undoneRise = r.undoSeq; }
        check (undoneQuieter != 0 && undoneRise == 0 && verdict.contains ("kept 1 fix that helped") && verdict.contains ("didn't help"),
               "the fix that didn't help is undone, the one that did is kept (" + verdict + ")");
        check (p->revertedFixes.size() == 1 && p->revertedFixes[0].first == suite::Fix::BreakdownQuieter && p->lastApplied.empty(),
               "and it won't be tried again");
        p->lastApplied = { { suite::Fix::IntroQuieter, 0u, p->sendFix (suite::Fix::IntroQuieter) } };
        p->problemsBeforeLastPass = 3;
        check (p->verifyLastPass (now).joinIntoString ("").contains ("kept 1"), "a fix whose problem remains but the arc improved overall stays");
    }

    section ("Masking resolved automatically");
    {
        suite::FixBus::reset();
        suite::Registry::reset();
        auto p = make();
        set (*p, Params::problemWatch, 0.0f);   // (the problem watcher resolves lasting masking too)
        suite::Registry::announce (9101, "Prima Materia", "Vox", suite::Status::roleSet, { 6.0f, 0, 0, 0 });
        masking::Clash c;
        c.carveId = 0x123; c.band = 5; c.carveTrack = "Pads"; c.keepTrack = "Vox"; c.text = "pads and vocal at 1 kHz";
        for (int i = 0; i < 3; ++i) p->watchMasking ({ c });
        check (suite::FixBus::latest() == 0 && p->maskingSuggestions()[0].contains ("try Carve"), "off: lasting masking is only suggested");
        set (*p, Params::autoMasking, 1.0f);
        p->watchMasking ({ c });
        suite::FixBus::Message m;
        check (suite::FixBus::read (suite::FixBus::latest(), m) && m.fix == suite::Fix::Carve && (m.target & 0xfffu) == 0x123u
                   && ((m.target >> 12) & 0xfu) == 5u && ((m.target >> 16) & 0x3u) == 1u
                   && p->maskingSuggestions()[0].contains ("resolved, Pads gives way while the vocal sings"),
               "on: the pads give way at 1 kHz, only while the vocal sings");
        suite::Registry::reset();
    }

    section ("Mix it: mud, harshness, translation");
    {
        suite::Registry::reset();
        auto bands = [] (float low, float mud, float mid, float high)
        {
            std::array<float, 10> b {};
            b[0] = low; b[1] = low; b[2] = mud; b[3] = mud; b[4] = mid; b[5] = mid; b[6] = high; b[7] = high; b[8] = high - 6; b[9] = high - 12;
            return b;
        };
        suite::Registry::announce (9201, "Prima Materia", "Pads", suite::Status::roleSet | suite::Status::hasBands, { 4.0f, 0, 0, 0 }, bands (-40, -12, -20, -24));
        suite::Registry::announce (9202, "Prima Materia", "Lead", suite::Status::roleSet | suite::Status::hasBands, { 5.0f, 0, 0, 0 }, bands (-50, -26, -18, -10));
        suite::Registry::announce (9203, "Prima Materia", "Bass", suite::Status::roleSet | suite::Status::hasBands, { 2.0f, 0, 0, 0 }, bands (-8, -30, -60, -70));
        suite::Registry::announce (9204, "Prima Materia", "Vox", suite::Status::roleSet | suite::Status::hasBands, { 6.0f, 0, 0, 0 }, bands (-60, -20, -60, -9));
        advisor::Bands diff {};
        diff[3] = 3.5f; diff[7] = 4.0f;   // the master: muddy at 250 Hz, harsh at 4 kHz
        const auto fixes = mixfix::toneFixes (diff, true, suite::Registry::list());
        check (fixes.size() == 2 && fixes[0].band == 3 && fixes[0].tracks.size() == 1 && fixes[0].tracks[0].track == "Pads",
               "mud: the pads put most at 250 Hz (the bass and vocal are never touched): " + (fixes.empty() ? juce::String() : fixes[0].text));
        check (fixes.size() == 2 && fixes[1].band == 7 && fixes[1].tracks[0].track == "Lead", "harshness at 4 kHz: the lead, not the vocal: " + (fixes.size() > 1 ? fixes[1].text : juce::String()));
        check (mixfix::toneFixes (advisor::Bands {}, true, suite::Registry::list()).empty(), "a balanced master gets nothing");

        const auto t = mixfix::translation (suite::Registry::list(), 4.0f).joinIntoString ("\n");
        check (t.contains ("the bass ('Bass') nearly vanishes") && t.contains ("In mono") && ! t.contains ("the lead ('Lead') loses"),
               "translation: the bass vanishes on earbuds, the mix loses level in mono; the lead holds up on a phone");

        suite::FixBus::reset();
        auto p = make();
        p->mixIt();
        check (p->apvts.getRawParameterValue (Params::autoMasking)->load() > 0.5f && p->mixItPending, "Mix it turns on masking and starts Finish the song");
        p->sendFix (suite::Fix::Carve, suite::packCarve (9201, 3));
        check (p->undoMixIt() >= 1 && p->apvts.getRawParameterValue (Params::autoMasking)->load() < 0.5f, "and Undo Mix it takes back every move it made");
        suite::Registry::reset();
    }

    section ("The problem watcher");
    {
        suite::FixBus::reset();
        suite::Registry::reset();
        suite::Registry::announce (7001, "Prima Materia", "Lead", suite::Status::roleSet | suite::Status::clipping, { 5.0f, 0, 0, 0 });
        suite::Registry::announce (7002, "Solve et Coagula", "Music", suite::Status::vocalBuried, { 0, 0, 0, 0 });
        suite::Registry::announce (7003, "Azoth", "FX", suite::Status::missingFiles, { 0, 0, 0, 0 });
        suite::Registry::announce (7004, "Kybalion", "Bass", 0, { 0, 0, 0, 0 });

        watcher::Master master;
        master.measured = true; master.truePeakDb = 0.8f; master.ceilingDb = -0.3f; master.monoLossDb = 4.5f; master.lowCorrelation = -0.4f;
        const auto found = watcher::scan (suite::Registry::list(), master);
        auto find = [&] (const juce::String& key) { return std::find_if (found.begin(), found.end(), [&] (const watcher::Problem& p) { return p.key.startsWith (key); }); };
        check (find ("clip") != found.end() && find ("clip")->automatic && find ("clip")->fix == suite::Fix::GainStage, "a clipping track: gain staging, by itself");
        check (find ("buried") != found.end() && find ("buried")->automatic && find ("buried")->fix == suite::Fix::VocalRide, "a buried vocal: the rider, by itself");
        check (find ("missing") != found.end() && ! find ("missing")->automatic, "missing samples: offered, not done by itself");
        check (find ("mono") != found.end() && ! find ("mono")->automatic && find ("phase") != found.end() && ! find ("phase")->automatic,
               "mono loss and a phase problem: offered");
        check (find ("peak") != found.end() && find ("peak")->fix == suite::Fix::None, "no limiter on a peaking master: said, nothing to click");

        auto p = make();
        MentalismProcessor::Report r = p->report();
        r.measuredSeconds = 10.0f;
        r.measurement.integratedLufs = -9.0f; r.measurement.truePeakDb = -1.0f; r.ceiling = -0.3f; r.measurement.monoLossDb = 4.5f; r.measurement.lowCorrelation = 0.8f;
        check (p->watchPass (r), "always on: the first pass fixes the safe ones");
        int gain = 0, ride = 0;
        for (uint32_t s = 1; s <= suite::FixBus::latest(); ++s)
        {
            suite::FixBus::Message m;
            if (suite::FixBus::read (s, m)) { gain += m.fix == suite::Fix::GainStage ? 1 : 0; ride += m.fix == suite::Fix::VocalRide ? 1 : 0; }
        }
        check (gain == 1 && ride == 1, "gain staging and the vocal rider sent once each");
        const auto offered = p->problems();
        check (offered.size() == 2, "missing samples and mono loss offered (" + juce::String ((int) offered.size()) + ")");
        const auto sent = suite::FixBus::latest();
        r.measuredSeconds = 14.0f;
        p->watchPass (r);
        check (suite::FixBus::latest() == sent, "the same problem isn't fixed again within 30 s");
        check (p->applyProblemFix ("mono") && suite::FixBus::latest() == sent + 1 && p->problems().size() == 1, "one click fixes an offered problem");
        r.measuredSeconds = 50.0f; p->watchPass (r);
        r.measuredSeconds = 90.0f; p->watchPass (r);
        const auto still = p->problems();
        check (std::any_of (still.begin(), still.end(), [] (const watcher::Problem& q) { return q.key.startsWith ("clip") && q.text.contains ("two fixes"); }),
               "a clip still there after two fixes is offered to you instead");
        bool logged = false;
        for (const auto& e : suite::MoveLog::read()) logged = logged || e.text.startsWith ("Problem watcher: 'Lead' is clipping");
        check (logged, "fixes and problems show in What changed");
        set (*p, Params::problemWatch, 0.0f);
        check (! p->watchPass (r) && p->problems().empty(), "off, it does nothing");
        suite::Registry::reset();
        suite::FixBus::reset();
    }

    section ("The conductor's judge, and section loudness");
    {
        suite::Conductor::reset();
        auto p = make();
        MentalismProcessor::Report r = p->report();
        r.tonalAvailable = true;
        r.shortDiff.fill (1.0f);
        check (suite::Conductor::request (4242u), "(another plugin takes the turn)");
        p->conductTick (r);            // sees the new turn: the master before
        r.shortDiff.fill (1.8f);       // the move took the master away from its target
        juce::Thread::sleep (3100);
        p->conductTick (r);
        check (suite::Conductor::verdictFor (4242u) == suite::Conductor::worse, "the move made the master worse: the verdict says so");
        suite::Conductor::release (4242u);
        suite::Conductor::reset();

        suite::FixBus::reset();
        suite::Registry::reset();
        MentalismProcessor::Report s = p->report();
        s.measuredSeconds = 10.0f;
        s.arrangementAvailable = true;
        s.map.totalBars = 16; s.map.count = 2;
        s.map.sections[0] = { 1, suite::SectionType::Breakdown };
        s.map.sections[1] = { 9, suite::SectionType::Drop };
        s.arrangement.complete = true;
        s.arrangement.problems = 2;
        s.arrangement.notes = { "The breakdown is only 2 dB under the drop", "The drop is shorter than a DJ phrase" };
        s.arrangement.fixes = { suite::Fix::ArcMatch, suite::Fix::ExtendSection };
        s.arrangement.fixData = { suite::packArc (0, -2.0f), suite::packArc (1, 8.0f) };
        p->watchPass (s);
        suite::FixBus::Message m;
        check (suite::FixBus::latest() == 1 && suite::FixBus::read (1, m) && m.fix == suite::Fix::ArcMatch, "a breakdown too loud against the drop: brought down by itself");
        const auto offeredNow = p->problems();
        check (std::any_of (offeredNow.begin(), offeredNow.end(), [] (const watcher::Problem& q) { return q.fix == suite::Fix::ExtendSection; }),
               "a structural change (extending a section) is offered, not made");
        suite::FixBus::reset();
    }

    section ("The balance loop");
    {
        suite::FixBus::reset();
        auto p = make();
        MentalismProcessor::Report r = p->report();
        r.tonalAvailable = true;
        r.shortDiff.fill (1.0f);
        check (! p->balancePass (r) && suite::FixBus::latest() == 0, "off: nothing is sent");
        set (*p, Params::autoBalance, 1.0f);
        check (p->balancePass (r) && suite::FixBus::latest() == 1, "on: each pass sends a balance step");
        suite::FixBus::Message m;
        check (suite::FixBus::read (1, m) && m.fix == suite::Fix::BalanceStep, "(a BalanceStep to every Prima Materia)");
        suite::FixBus::acknowledge (1);   // two tracks took a step
        suite::FixBus::acknowledge (1);
        r.shortDiff.fill (1.2f);
        check (p->balancePass (r) && p->balanceKept.load() == 1 && suite::FixBus::latest() == 2, "the master stayed near its target: kept, and the next step goes out");
        suite::FixBus::acknowledge (2);
        r.shortDiff.fill (2.5f);
        suite::FixBus::Message undo;
        check (p->balancePass (r) && p->balanceUndone.load() == 1 && suite::FixBus::read (3, undo) && undo.undo && (undo.target & 0xffffffu) == 2u,
               "the master moved away from its target: that step is undone");
        int quiet = 0;
        for (int i = 0; i < 4; ++i) quiet += p->balancePass (r) ? 0 : 1;
        check (quiet == 4 && p->balancePass (r), "it rests a few passes, then tries again");
        check (! p->balancePass (r) && suite::FixBus::latest() == 4, "nobody took the step (all balanced): it rests");
        bool logged = false;
        for (const auto& e : suite::MoveLog::read()) logged = logged || (e.plugin == "Mentalism" && e.text.startsWith ("Undid a balance step"));
        check (logged, "kept and undone steps show in What changed");
        suite::FixBus::reset();
    }

    section ("Pass-through");
    {
        auto p = make();
        juce::AudioBuffer<float> audio, refBuf;
        render (*p, 0.2, sine (440.0f, -6.0f), &audio);
        refBuf.setSize (2, audio.getNumSamples()); refBuf.clear(); sine (440.0f, -6.0f) (refBuf, 0);
        float diff = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i) diff = std::max (diff, std::abs (audio.getSample (0, i) - refBuf.getSample (0, i)));
        check (diff < 1.0e-9f, "audio is bit-identical");
    }

    if (outDir != juce::File())
    {
        auto p = make();
        // A rough "mix": kick-ish thump, bass, a slightly wide supersaw-ish lead, hats.
        juce::Random rng (2);
        render (*p, 14.0, [&] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto beat = std::fmod (t, 60.0 / 138.0);
                const auto kick = 0.6f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * beat) * std::exp (-beat * 18.0));
                const auto bass = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t) * (beat > 0.2 ? 1.0f : 0.2f);
                float lead = 0.0f;
                for (int k = 0; k < 5; ++k) lead += 0.03f * (float) (2.0 * std::fmod (t * 440.0 * (1.0 + 0.005 * (k - 2)), 1.0) - 1.0);
                const auto hat = 0.04f * (rng.nextFloat() * 2.0f - 1.0f) * (std::fmod (t, 0.217) < 0.03 ? 1.0f : 0.0f);
                b.setSample (0, i, kick + bass + lead * 1.1f + hat);
                b.setSample (1, i, kick + bass + lead * 0.9f + hat);
            }
        });
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("mentalism.png"));
        // Arrangement view, with a map and a played arrangement.
        {
            suite::SectionMap map;
            map.totalBars = 8; map.count = 4;
            map.sections[0] = { 1, suite::SectionType::Groove };
            map.sections[1] = { 3, suite::SectionType::Breakdown };
            map.sections[2] = { 5, suite::SectionType::Build };
            map.sections[3] = { 7, suite::SectionType::Drop };
            suite::SectionBus::publish (map);
            auto q = make();
            FakePlayHead head;
            head.bpm = 120.0;
            q->setPlayHead (&head);
            render (*q, 16.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto t = (double) (pos + i) / sampleRate;
                    const auto db = t < 4.0 ? -16.0f : t < 8.0 ? -13.5f : t < 12.0 ? -20.0f + 6.0f * (float) ((t - 8.0) / 4.0) : -12.0f;
                    const auto v = juce::Decibels::decibelsToGain (db) * (float) std::sin (juce::MathConstants<double>::twoPi * 500.0 * t);
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            }, nullptr, &head);
            q->setPlayHead (nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q->createEditor());
            if (auto* tabs = dynamic_cast<suite::ui::TabBar*> (ed->findChildWithID ("tabs"))) tabs->setSelected (1);
            snapshot (*ed, outDir.getChildFile ("mentalism_arrangement.png"));
            ed.reset();
            suite::SectionBus::withdraw();
        }
        editor.reset();
    }

    return finish();
}
