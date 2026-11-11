// Offline tests for Mentalism. Usage: MentalismTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestUtils.h"
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
