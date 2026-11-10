// Offline tests for Mentalism. Usage: MentalismTests [output-dir]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestUtils.h"
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
