// Offline tests for Antioch. Usage: AntiochTests [output-dir]

#include "PluginProcessor.h"
#include "SuiteUI.h"
#include "SectionBus.h"
#include "TestUtils.h"
#include "ChordBus.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<AntiochProcessor> make()
    {
        suite::SectionBus::withdraw();     // no leftover arrangement from another test
        auto p = std::make_unique<AntiochProcessor>();
        set (*p, Params::followKey, 0.0f); // tests are independent of other plugins
        prepare (*p);
        return p;
    }

    void soloSection (AntiochProcessor& p, float echo, float verb, float thr)
    {
        set (p, Params::dry, 0.0f);
        set (p, Params::echoLevel, echo);
        set (p, Params::verbLevel, verb);
        set (p, Params::throwLevel, thr);
        set (p, Params::echoToVerb, 0.0f);
        set (p, Params::Throw::toVerb, 0.0f);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Default patch, plucked note");
    {
        auto p = make();
        juce::AudioBuffer<float> audio;
        const auto s = render (*p, 16.0, pluck(), &audio);
        check (s.finite, "output is finite");
        check (s.peak > 0.05f && s.peak < 2.0f, "peak in sane range (" + juce::String (s.peak, 3) + ")");
        check (s.tailPeak < 0.01f, "tail decays (last second peak " + juce::String (s.tailPeak, 5) + ")");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("antioch_default.wav"), audio);
    }

    section ("Worst case: max feedback, resonance, shimmer; full-scale noise");
    {
        auto p = make();
        for (int t = 0; t < Params::numTaps; ++t)
        {
            set (*p, Params::tapId (t, Params::Tap::on), 1.0f);
            set (*p, Params::tapId (t, Params::Tap::feedback), 0.95f);
            set (*p, Params::tapId (t, Params::Tap::pitch), t % 2 == 0 ? 12.0f : -24.0f);
            set (*p, Params::tapId (t, Params::Tap::level), 1.0f);
            set (*p, Params::tapId (t, Params::Tap::harmonic), 16.0f);
        }
        set (*p, Params::resonance, 0.97f);
        set (*p, Params::resMix, 1.0f);
        set (*p, Params::echoToVerb, 1.0f);
        set (*p, Params::Verb::decay, 20.0f);
        set (*p, Params::Verb::shimmer, 1.0f);
        set (*p, Params::Verb::shimInt, 3.0f);
        set (*p, Params::verbLevel, 1.0f);
        set (*p, Params::Throw::on, 1.0f);
        set (*p, Params::Throw::feedback, 0.95f);
        set (*p, Params::Throw::drive, 1.0f);
        set (*p, Params::output, 12.0f);

        juce::Random rng (1);
        const auto s = render (*p, 20.0, [&] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            if (pos > (int64_t) sampleRate * 2) return;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        });
        check (s.finite, "output is finite");
        check (s.peak < 60.0f, "bounded peak (" + juce::String (s.peak, 2) + ")");
    }

    section ("Reverb: decays, and ducks under the dry signal");
    {
        auto p = make();
        soloSection (*p, 0.0f, 1.0f, 0.0f);
        set (*p, Params::Verb::duck, 0.0f);
        set (*p, Params::Verb::shimmer, 0.0f);
        set (*p, Params::Verb::decay, 3.0f);
        juce::AudioBuffer<float> audio;
        render (*p, 6.0, pluck(), &audio);
        const auto early = rmsBetween (audio, 0.2, 0.6), late = rmsBetween (audio, 3.2, 3.6);
        check (early > 0.005f, "reverb produces a tail (rms " + juce::String (early, 4) + ")");
        // RT60 = 3 s -> ~60 dB over 3 s; allow generous tolerance.
        const auto dropDb = juce::Decibels::gainToDecibels (late / early);
        check (dropDb < -35.0f && dropDb > -90.0f, "decays ~60 dB over the decay time (" + juce::String (dropDb, 1) + " dB)");

        auto sustained = [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto v = t < 2.0 ? (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t)) : 0.0f;
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
        };

        float during[2] {}, after[2] {};
        for (int duck = 0; duck < 2; ++duck)
        {
            auto q = make();
            soloSection (*q, 0.0f, 1.0f, 0.0f);
            set (*q, Params::Verb::shimmer, 0.0f);
            set (*q, Params::Verb::duck, (float) duck);
            juce::AudioBuffer<float> a;
            render (*q, 3.0, sustained, &a);
            during[duck] = rmsBetween (a, 1.2, 1.9);
            after[duck] = rmsBetween (a, 2.5, 2.9);
        }
        const auto duckDb = juce::Decibels::gainToDecibels (during[1] / during[0]);
        check (duckDb < -10.0f, "ducked while dry plays (" + juce::String (duckDb, 1) + " dB)");
        check (after[1] > 0.5f * after[0], "tail blooms back after dry stops");
    }

    section ("Shimmer at maximum still dies away (no self-oscillation)");
    {
        for (float decay : { 2.0f, 6.0f, 20.0f })
        {
        auto p = make();
        soloSection (*p, 0.0f, 1.0f, 0.0f);
        set (*p, Params::Verb::decay, decay);
        set (*p, Params::Verb::size, 1.0f);
        set (*p, Params::Verb::shimmer, 1.0f);
        set (*p, Params::Verb::duck, 0.0f);
        juce::AudioBuffer<float> audio;
        const auto s = render (*p, 60.0, pluck(), &audio);
        const auto peak = peakBetween (audio, 0.0, 10.0), end = peakBetween (audio, 55.0, 60.0);
        const auto label = "decay " + juce::String (decay, 0) + " s: ";
        check (s.finite && peak < 2.0f, label + "shimmer level stays reasonable (peak " + juce::String (peak, 3) + ")");
        check (end < 0.5f * peak, label + "shimmer tail decays over time (end " + juce::String (end, 4) + ")");
        if (outDir != juce::File() && decay > 10.0f) writeWav (outDir.getChildFile ("antioch_shimmer.wav"), audio);
        }
    }

    section ("Throw: silent until thrown, then rings out");
    {
        auto p = make();
        soloSection (*p, 0.0f, 0.0f, 1.0f);
        juce::AudioBuffer<float> audio;
        render (*p, 2.0, pluck(), &audio);
        check (peakBetween (audio, 0.0, 2.0) < 1.0e-4f, "nothing enters the delay without a throw");

        auto q = make();
        soloSection (*q, 0.0f, 0.0f, 1.0f);
        set (*q, Params::Throw::on, 1.0f);
        juce::AudioBuffer<float> thrown;
        render (*q, 4.0, pluck(), &thrown, nullptr, [&] (int64_t pos)
        {
            if (pos > (int64_t) (0.6 * sampleRate)) set (*q, Params::Throw::on, 0.0f); // release the button
        });
        check (peakBetween (thrown, 0.8, 2.5) > 0.02f, "thrown note keeps echoing after release ("
                                                           + juce::String (peakBetween (thrown, 0.8, 2.5), 3) + ")");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("antioch_throw.wav"), thrown);
    }

    section ("Auto throw: catches only the phrase end");
    {
        auto p = make();
        soloSection (*p, 0.0f, 0.0f, 1.0f);
        set (*p, Params::Throw::autoMode, 1.0f);   // every 2 bars
        set (*p, Params::Throw::autoLen, 2.0f);    // last beat
        set (*p, Params::Throw::feedback, 0.0f);
        FakePlayHead head;
        head.bpm = 120.0;                           // 2 bars = 4 s, last beat = 3.5..4.0 s
        p->setPlayHead (&head);

        juce::Random rng (7);
        juce::AudioBuffer<float> audio;
        render (*p, 4.6, [&] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));
        }, &audio, &head);
        p->setPlayHead (nullptr);

        check (peakBetween (audio, 0.0, 3.5) < 1.0e-3f, "silent before the throw window");
        check (peakBetween (audio, 3.9, 4.6) > 0.05f, "throw fires at the phrase end");
    }

    section ("Auto throw at Ouroboros section ends");
    {
        suite::SectionMap map;   // sections of 2 and 3 bars at 120 BPM (2 s per bar)
        map.totalBars = 5; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 3, suite::SectionType::Drop };

        auto p = make();
        soloSection (*p, 0.0f, 0.0f, 1.0f);
        set (*p, Params::Throw::autoMode, (float) Params::Throw::autoAtSectionEnds);
        set (*p, Params::Throw::autoLen, 2.0f);    // last beat
        set (*p, Params::Throw::feedback, 0.0f);
        suite::SectionBus::publish (map);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::Random rng (11);
        juce::AudioBuffer<float> audio;
        render (*p, 10.6, [&] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));
        }, &audio, &head);
        p->setPlayHead (nullptr);
        suite::SectionBus::withdraw();

        check (peakBetween (audio, 0.0, 3.5) < 1.0e-3f, "silent through the first section");
        check (peakBetween (audio, 3.9, 4.6) > 0.05f, "throws at the end of the 2-bar Groove (4 s)");
        check (peakBetween (audio, 4.8, 9.4) < 1.0e-3f, "silent through the Drop until its end");
        check (peakBetween (audio, 9.9, 10.6) > 0.05f, "throws at the end of the Drop (10 s)");
    }

    section ("Section scenes");
    {
        suite::SectionMap map;   // Groove bars 1-2, Breakdown 3-4 (120 BPM: 2 s per bar)
        map.totalBars = 4; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 3, suite::SectionType::Breakdown };

        auto p = make();
        set (*p, suite::SceneEngine::morphId, 0.0f);       // instant, for a crisp measurement
        const auto base = p->apvts.getParameter (Params::verbLevel)->getValue();
        suite::SectionBus::publish (map);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        float inGroove = -1.0f, inBreakdown = -1.0f;
        render (*p, 7.0, pluck(), nullptr, &head, [&] (int64_t pos)
        {
            const auto t = (double) pos / sampleRate;
            if (inGroove < 0.0f && t > 2.0) inGroove = p->scenes.getEffective (AntiochProcessor::sVerb);
            if (inBreakdown < 0.0f && t > 5.0) inBreakdown = p->scenes.getEffective (AntiochProcessor::sVerb);
        });
        check (std::abs (inGroove - base) < 1.0e-4f, "Groove has no reverb offset");
        check (std::abs (inBreakdown - (base + 0.25f)) < 1.0e-3f, "Breakdown scene adds +25% reverb ("
                                                                       + juce::String (base, 2) + " -> " + juce::String (inBreakdown, 2) + ")");
        check (std::abs (p->apvts.getParameter (Params::verbLevel)->getValue() - base) < 1.0e-6f, "the knob itself doesn't move");

        set (*p, suite::SceneEngine::enabledId, 0.0f);
        head.ppq = 4.0 * 2.5;
        render (*p, 0.1, {}, nullptr, &head);
        check (std::abs (p->scenes.getEffective (AntiochProcessor::sVerb) - base) < 1.0e-4f, "Scenes off: back to the knob value");
        p->setPlayHead (nullptr);
        suite::SectionBus::withdraw();

        // Drop 2+ adds to the Drop scene, only in the second drop.
        {
            suite::SectionMap twoDrops;   // Drop 1-2, Breakdown 3, Drop 4-5 (2 s per bar)
            twoDrops.totalBars = 5; twoDrops.count = 3;
            twoDrops.sections[0] = { 1, suite::SectionType::Drop };
            twoDrops.sections[1] = { 3, suite::SectionType::Breakdown };
            twoDrops.sections[2] = { 4, suite::SectionType::Drop };
            auto d = make();
            set (*d, suite::SceneEngine::morphId, 0.0f);
            const auto echoBase = d->apvts.getParameter (Params::echoLevel)->getValue();
            suite::SectionBus::publish (twoDrops);
            FakePlayHead h;
            h.bpm = 120.0;
            d->setPlayHead (&h);
            float first = -1.0f, second = -1.0f;
            render (*d, 9.5, {}, nullptr, &h, [&] (int64_t pos)
            {
                const auto t = (double) pos / sampleRate;
                if (first < 0.0f && t > 2.0) first = d->scenes.getEffective (AntiochProcessor::sEcho);
                if (second < 0.0f && t > 8.0) second = d->scenes.getEffective (AntiochProcessor::sEcho);
            });
            d->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            check (std::abs (first - echoBase) < 1.0e-3f && std::abs (second - (echoBase + 0.1f)) < 1.0e-3f,
                   "Drop 2+ scene: more echo in the second drop only (" + juce::String (first, 2) + " -> " + juce::String (second, 2) + ")");
        }

        p->scenes.setOffset ((int) suite::SectionType::Drop, AntiochProcessor::sEcho, -0.4f);
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (q->scenes.getOffset ((int) suite::SectionType::Drop, AntiochProcessor::sEcho) + 0.4f) < 1.0e-3f,
               "scenes are saved with the project");
    }

    section ("Reverb freeze into breakdowns");
    {
        suite::SectionMap map;   // 120 BPM: Drop bars 1-2 (0-4 s), Breakdown bars 3-6 (4-12 s)
        map.totalBars = 6; map.count = 2;
        map.sections[0] = { 1, suite::SectionType::Drop };
        map.sections[1] = { 3, suite::SectionType::Breakdown };
        auto run = [&] (int freezeChoice)
        {
            auto p = make();
            soloSection (*p, 0.0f, 1.0f, 0.0f);
            set (*p, suite::SceneEngine::enabledId, 0.0f);
            set (*p, Params::Verb::decay, 1.0f);
            set (*p, Params::Verb::freeze, (float) freezeChoice);
            suite::SectionBus::publish (map);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::Random rng (5);
            juce::AudioBuffer<float> audio;
            render (*p, 12.5, [&] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto v = (double) (pos + i) / sampleRate < 3.9 ? 0.3f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f;
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            }, &audio, &head);
            p->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            return std::make_pair (audio, p->freezesPlayed.load());
        };
        const auto [plain, none] = run (0);
        const auto [frozen, played] = run (3);   // 4 bars
        const auto tail = rmsBetween (plain, 6.0, 7.0), wash = rmsBetween (frozen, 6.0, 7.0);
        check (none == 0 && played == 1, "the freeze fires once, entering the breakdown");
        const auto inDrop = rmsBetween (plain, 3.0, 3.9);
        check (wash > 0.25f * inDrop && wash > 10.0f * tail, "the drop's last moment hangs on as a wash well after the reverb would have died ("
                   + juce::String (wash, 4) + " vs " + juce::String (tail, 5) + "; the reverb in the drop: " + juce::String (inDrop, 4) + ")");
        check (rmsBetween (frozen, 6.0, 7.0) > rmsBetween (frozen, 10.0, 11.0) && rmsBetween (frozen, 12.1, 12.5) < 1.0e-4f,
               "and fades out over the 4 bars");
    }

    section ("Reverb tension width");
    {
        suite::Progression prog;
        prog.root = 9; prog.scale = 0; prog.count = 2; prog.quartersPerChord = 4.0;
        prog.chordRoots[0] = 0; prog.chordRoots[1] = 7;   // i, then v
        auto p = make();
        set (*p, Params::Verb::width, 1.0f);
        set (*p, Params::Verb::tension, 1.0f);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        suite::ChordBus::publish (prog);
        render (*p, 1.8, {}, nullptr, &head);
        const auto home = p->reverbWidthNow.load();
        render (*p, 1.8, {}, nullptr, &head);
        const auto away = p->reverbWidthNow.load();
        suite::ChordBus::withdraw();
        p->setPlayHead (nullptr);
        check (std::abs (home - 1.0f) < 0.02f && away > 1.45f, "the reverb opens out on the v chord (" + juce::String (home, 2) + " -> " + juce::String (away, 2) + ")");
    }

    section ("Key follow via KeyBus");
    {
        auto p = make();
        set (*p, Params::followKey, 1.0f);
        set (*p, Params::rootNote, 2.0f); // local D
        suite::KeyBus::publish (9, 0);    // Correspondence says A minor
        render (*p, 0.05, {});
        check (p->followingBus.load() && p->effectiveRoot.load() == 9, "follows the broadcast key (A)");

        set (*p, Params::followKey, 0.0f);
        render (*p, 0.05, {});
        check (! p->followingBus.load() && p->effectiveRoot.load() == 2, "manual override uses the local key (D)");

        suite::KeyBus::withdraw();
        set (*p, Params::followKey, 1.0f);
        render (*p, 0.05, {});
        check (! p->followingBus.load() && p->effectiveRoot.load() == 2, "falls back to the local key when nothing broadcasts");
    }

    section ("Final-drop key change");
    {
        suite::SectionMap map;   // Drop 1-2, Breakdown 3-4, Drop 5-6 (the last drop), key +2 from there
        map.totalBars = 6; map.count = 3; map.finalLift = 2;
        map.sections[0] = { 1, suite::SectionType::Drop };
        map.sections[1] = { 3, suite::SectionType::Breakdown };
        map.sections[2] = { 5, suite::SectionType::Drop };
        auto p = make();
        suite::SectionBus::publish (map);
        suite::KeyBus::publish (9, 0);   // A minor
        set (*p, Params::followKey, 1.0f);
        FakePlayHead head;
        head.ppq = 2.0;                  // first drop
        p->setPlayHead (&head);
        render (*p, 0.05, {});
        const auto before = p->effectiveRoot.load();
        head.ppq = 4.0 * 4.0 + 2.0;      // last drop
        head.playing = true;
        render (*p, 0.05, {});
        const auto after = p->effectiveRoot.load();
        p->setPlayHead (nullptr);
        check (before == 9 && after == 11, "harmonies move from A to B in the last drop (" + juce::String (before) + " -> " + juce::String (after) + ")");
        suite::SectionBus::withdraw();
        suite::KeyBus::withdraw();
    }

    section ("State save/restore");
    {
        auto a = make();
        set (*a, Params::tapId (2, Params::Tap::pitch), -5.0f);
        set (*a, Params::Verb::decay, 11.0f);
        juce::MemoryBlock state;
        a->getStateInformation (state);

        auto b = make();
        b->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (b->apvts.getRawParameterValue (Params::tapId (2, Params::Tap::pitch))->load() + 5.0f) < 0.01f, "tap pitch restored");
        check (std::abs (b->apvts.getRawParameterValue (Params::Verb::decay)->load() - 11.0f) < 0.01f, "reverb decay restored");
    }

    if (outDir != juce::File())
    {
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        auto* tabs = dynamic_cast<suite::ui::TabBar*> (editor->findChildWithID ("tabs"));
        const char* names[] = { "antioch_echo.png", "antioch_reverb.png", "antioch_throw.png", "antioch_scenes.png" };
        for (int i = 0; i < 4; ++i)
        {
            if (tabs != nullptr) tabs->setSelected (i);
            snapshot (*editor, outDir.getChildFile (names[i]));
        }
        editor.reset();
        std::cout << "Wrote renders and UI snapshots to " << outDir.getFullPathName() << std::endl;
    }

    return finish();
}
