// Offline tests for Antioch. Usage: AntiochTests [output-dir]

#include "PluginProcessor.h"
#include "AutopilotBus.h"
#include "SuiteUI.h"
#include "SectionBus.h"
#include "TestUtils.h"
#include "ChordBus.h"
#include "KickBus.h"
#include "VocalBus.h"
#include "ActivityBus.h"

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

    section ("Reverb pumps with the shared kick");
    {
        auto noise = [] (juce::Random& rng)
        {
            return [&rng] (juce::AudioBuffer<float>& b, int64_t)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        b.setSample (ch, i, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));
            };
        };
        auto run = [&] (float pump)
        {
            auto p = make();
            soloSection (*p, 0.0f, 1.0f, 0.0f);
            set (*p, Params::Verb::duck, 0.0f);
            set (*p, Params::Verb::pump, pump);
            FakePlayHead head;
            head.bpm = 120.0;   // a kick every half second
            p->setPlayHead (&head);
            juce::Random rng (3);
            juce::AudioBuffer<float> audio;
            render (*p, 4.0, noise (rng), &audio, &head);
            p->setPlayHead (nullptr);
            return std::make_pair (std::move (audio), p->pumpNow.load());
        };
        suite::KickPatternSet kp;
        kp.valid = true;
        auto& any = kp.bySection[(size_t) suite::KickPatternSet::anySection];
        any.count = 4; any.positions = { 0.0f, 1.0f, 2.0f, 3.0f }; any.kickBars = 4;
        suite::KickBus::publish (kp);
        suite::Autopilot::set (suite::Autopilot::mixing, false);   // (with the autopilot the pump never goes below 35%)
        const auto [off, offMin] = run (0.0f);
        suite::Autopilot::set (suite::Autopilot::mixing, true);
        const auto [auto0, autoMin] = run (0.0f);
        const auto [on, onMin] = run (1.0f);
        suite::KickBus::withdraw();
        const auto [noKick, noKickMin] = run (1.0f);

        const auto onDuck = juce::Decibels::gainToDecibels (rmsBetween (on, 3.005, 3.04) / rmsBetween (on, 3.4, 3.48));
        const auto offDuck = juce::Decibels::gainToDecibels (rmsBetween (off, 3.005, 3.04) / rmsBetween (off, 3.4, 3.48));
        check (onDuck < offDuck - 8.0f && onMin < 1.0f, "the tail ducks right after each kick (" + juce::String (onDuck - offDuck, 1) + " dB)");
        check (offMin > 0.999f && noKickMin > 0.999f, "no pump when it's off, or when no kick is shared");
        const auto autoDuck = juce::Decibels::gainToDecibels (rmsBetween (auto0, 3.005, 3.04) / rmsBetween (auto0, 3.4, 3.48));
        check (autoMin < 1.0f && autoDuck < offDuck - 2.0f, "with the autopilot's mixing on, the tail pumps with the kick even with Pump at 0 ("
                   + juce::String (autoDuck - offDuck, 1) + " dB)");
    }

    section ("Throws at vocal phrase ends");
    {
        suite::VocalBus::clear();
        suite::VocalBus::mark (1, true);   // the vocal sings bars 1-2, rests in 3, sings 4
        suite::VocalBus::mark (2, true);
        suite::VocalBus::mark (3, false);
        suite::VocalBus::mark (4, true);
        auto p = make();
        soloSection (*p, 0.0f, 0.0f, 1.0f);
        set (*p, Params::Throw::autoMode, (float) Params::Throw::autoAtVocalEnds);
        set (*p, Params::Throw::autoLen, 2.0f);    // last beat
        set (*p, Params::Throw::feedback, 0.0f);
        FakePlayHead head;
        head.bpm = 120.0;   // 2 s per bar
        p->setPlayHead (&head);
        juce::Random rng (5);
        juce::AudioBuffer<float> audio;
        render (*p, 9.0, [&] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));
        }, &audio, &head);
        p->setPlayHead (nullptr);
        suite::VocalBus::clear();
        check (peakBetween (audio, 0.0, 3.5) < 1.0e-3f, "nothing in the middle of the phrase (bar 1)");
        check (peakBetween (audio, 3.9, 4.6) > 0.05f, "throws the last beat of bar 2, where the phrase ends");
        check (peakBetween (audio, 4.8, 7.4) < 1.0e-3f, "nothing in the rest (bar 3) or before the next phrase ends");
        check (peakBetween (audio, 7.9, 8.6) > 0.05f, "throws the end of the next phrase (bar 4)");
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

    section ("Throw moments");
    {
        suite::SectionMap m;
        m.totalBars = 4; m.count = 1;
        m.sections[0] = { 1, suite::SectionType::Groove };
        m.momentCount = 1;
        m.moments[0] = { 3, suite::momentThrow };   // bar 3 at 4 s (120 BPM): throw the last beat before it
        auto p = make();
        soloSection (*p, 0.0f, 0.0f, 1.0f);
        set (*p, Params::Throw::autoLen, 2.0f);
        set (*p, Params::Throw::feedback, 0.0f);
        suite::SectionBus::publish (m);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::Random rng (13);
        juce::AudioBuffer<float> audio;
        render (*p, 4.8, [&] (juce::AudioBuffer<float>& b, int64_t)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, 0.3f * (rng.nextFloat() * 2.0f - 1.0f));
        }, &audio, &head);
        p->setPlayHead (nullptr);
        suite::SectionBus::withdraw();
        check (peakBetween (audio, 0.0, 3.5) < 1.0e-3f && peakBetween (audio, 3.9, 4.6) > 0.05f, "a throw moment throws the beat before its bar, and nothing else ("
                   + juce::String (peakBetween (audio, 0.0, 3.5), 4) + " before, " + juce::String (peakBetween (audio, 3.9, 4.6), 4) + " at the end)");
    }

    section ("Learning from your tweaks");
    {
        suite::SectionMap m;
        m.totalBars = 4; m.count = 2;
        m.sections[0] = { 1, suite::SectionType::Breakdown };
        m.sections[1] = { 3, suite::SectionType::Drop };
        auto p = make();
        suite::SectionBus::publish (m);
        int verb = -1;
        for (int i = 0; i < p->scenes.getCount(); ++i) if (p->scenes.getParamId (i) == Params::verbLevel) verb = i;
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        render (*p, 1.0, {}, nullptr, &head);   // in the breakdown
        p->pollTick();
        auto* param = p->apvts.getParameter (Params::verbLevel);
        const auto before = param->getValue();
        const auto breakdownOffset = p->scenes.getOffset ((int) suite::SectionType::Breakdown, verb);
        param->setValueNotifyingHost (before + 0.2f);   // you turn the reverb up in the breakdown
        render (*p, 0.5, {}, nullptr, &head);
        p->pollTick();
        check (! p->scenes.getProposal().valid, "nothing is offered while the section is still playing");
        render (*p, 3.0, {}, nullptr, &head);   // into the drop
        p->pollTick();
        p->setPlayHead (nullptr);
        suite::SectionBus::withdraw();
        check (verb >= 0 && p->scenes.getProposal().valid && p->scenes.getProposal().type == (int) suite::SectionType::Breakdown
                   && p->scenes.proposalText().contains ("in every breakdown"),
               "at the end of the breakdown: " + p->scenes.proposalText());
        p->scenes.acceptProposal();
        check (std::abs (param->getValue() - before) < 0.01f
                   && std::abs (p->scenes.getOffset ((int) suite::SectionType::Breakdown, verb) - (breakdownOffset + 0.2f)) < 0.01f,
               "accepting moves the change into the breakdown scene and puts the knob back for the other sections");
        check (! p->scenes.getProposal().valid, "and the offer goes away");
    }

    section ("Lead-aware space, chord-aware resonators, level-neutral scenes");
    {
        auto runLead = [] (float lead)
        {
            auto p = make();
            soloSection (*p, 0.0f, 1.0f, 0.0f);
            set (*p, Params::Verb::duck, 0.0f);
            juce::Random rng (3);
            juce::AudioBuffer<float> a;
            render (*p, 2.0, [&rng] (juce::AudioBuffer<float>& b, int64_t)
            {
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));
            }, &a, nullptr, [lead] (int64_t) { suite::ActivityBus::write (suite::ActivityBus::lead, lead); });
            suite::ActivityBus::clear();
            return std::make_pair (rmsBetween (a, 1.5, 2.0), p->leadRoomNow.load());
        };
        const auto [quiet, quietGain] = runLead (0.0f);
        const auto [playing, playingGain] = runLead (1.0f);
        check (std::abs (quietGain - 1.0f) < 0.01f && std::abs (playingGain - 0.7f) < 0.02f && playing < quiet * 0.8f,
               "while the lead plays the reverb steps back (x" + juce::String (playingGain, 2) + "), and blooms when it rests");

        suite::Progression prog;
        prog.root = 9; prog.scale = 0; prog.count = 2; prog.quartersPerChord = 4.0;
        prog.chordRoots[0] = 0; prog.chordRoots[1] = 8;   // i, then VI
        auto p = make();
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        suite::ChordBus::publish (prog);
        render (*p, 1.0, {}, nullptr, &head);
        const auto onI = p->chordShift.load();
        render (*p, 2.0, {}, nullptr, &head);
        const auto onVI = p->chordShift.load();
        suite::ChordBus::withdraw();
        p->setPlayHead (nullptr);
        check (onI == 0 && onVI == -4, "the resonators follow the chord: on VI they ring 4 semitones down (" + juce::String (onVI) + ")");

        auto neutral = [] (bool on)
        {
            suite::SectionMap m;
            m.totalBars = 4; m.count = 2;
            m.sections[0] = { 1, suite::SectionType::Drop };
            m.sections[1] = { 3, suite::SectionType::Breakdown };
            auto q = make();
            soloSection (*q, 0.0f, 0.4f, 0.0f);   // reverb only (at 40%, so the scene has room to raise it)
            suite::SectionBus::publish (m);
            int verb = -1;
            for (int i = 0; i < q->scenes.getCount(); ++i) if (q->scenes.getParamId (i) == Params::verbLevel) verb = i;
            q->scenes.clearAll();
            q->scenes.setOffset ((int) suite::SectionType::Breakdown, verb, 0.5f);   // the breakdown scene: much more reverb
            set (*q, suite::SceneEngine::levelId, on ? 1.0f : 0.0f);
            FakePlayHead h;
            h.bpm = 120.0;
            q->setPlayHead (&h);
            juce::Random rng (8);
            render (*q, 7.5, [&rng] (juce::AudioBuffer<float>& b, int64_t)
            {
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));
            }, nullptr, &h);
            q->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            return q->scenes.levelGainNow.load();
        };
        const auto offGain = neutral (false), onGain = neutral (true);
        check (std::abs (offGain) < 0.01f && onGain < -0.5f,
               "level-neutral scenes: the louder breakdown scene is brought back towards the drop's level (" + juce::String (onGain, 1) + " dB); off, nothing");
    }

    section ("Role-aware setup");
    {
        auto named = [] (suite::SuiteProcessor& p, const char* name)
        {
            juce::AudioProcessor::TrackProperties props;
            props.name = juce::String (name);
            p.updateTrackProperties (props);
        };
        AntiochProcessor vox;
        vox.captureFreshValues();
        named (vox, "Lead Vox");
        check (vox.tryRoleSetup() && (int) vox.apvts.getRawParameterValue (Params::Throw::autoMode)->load() == Params::Throw::autoAtVocalEnds,
               "a new Antioch on a vocal throws the end of every sung phrase");
        AntiochProcessor pad;
        pad.captureFreshValues();
        named (pad, "Pads");
        check (pad.tryRoleSetup() && (int) pad.apvts.getRawParameterValue (Params::Throw::autoMode)->load() == 0
                   && pad.apvts.getRawParameterValue (Params::autoSpace)->load() > 0.5f, "on pads: no throws, but its space sets itself");
    }

    section ("Auto space level");
    {
        auto p = make();
        juce::AudioProcessor::TrackProperties props;
        props.name = "Lead";
        p->updateTrackProperties (props);
        set (*p, Params::autoSpace, 1.0f);
        set (*p, Params::echoLevel, 1.0f);   // far too much space for a lead
        set (*p, Params::verbLevel, 1.0f);
        set (*p, Params::throwLevel, 0.0f);
        auto plucks = [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto inNote = std::fmod (t, 0.5);
                const auto v = 0.3f * (float) (std::sin (juce::MathConstants<double>::twoPi * 440.0 * t) * std::exp (-inNote * 6.0));
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
        int64_t at = 0;
        const auto before = [&] { render (*p, 4.3, [&] (juce::AudioBuffer<float>& b, int64_t pos) { plucks (b, at + pos); }); at += (int64_t) (4.3 * sampleRate); }; 
        before();
        const auto tailBefore = p->spaceTailDb.load();
        for (int i = 0; i < 16; ++i) { before(); p->spacePass(); }
        before();
        const auto tailAfter = p->spaceTailDb.load();
        const auto trim = p->apvts.getRawParameterValue (Params::spaceTrim)->load();
        check (trim < -3.0f && std::abs (tailAfter + 12.0f) < std::abs (tailBefore + 12.0f) - 3.0f && p->spaceKept.load() >= 2,
               "on a lead with too much space it brings the echo and reverb down, a checked step at a time (tail "
                   + juce::String (tailBefore, 1) + " -> " + juce::String (tailAfter, 1) + " dB under the dry, target -12; trim " + juce::String (trim, 1) + " dB)");
        auto off = make();
        off->updateTrackProperties (props);
        set (*off, Params::echoLevel, 1.0f);
        render (*off, 4.3, plucks);
        check (! off->spacePass() && std::abs (off->apvts.getRawParameterValue (Params::spaceTrim)->load()) < 0.01f, "off, it leaves the space alone");
    }

    section ("Glue room on the music bus");
    {
        auto bus = make();
        bus->captureFreshValues();
        juce::AudioProcessor::TrackProperties props;
        props.name = "Music Bus";
        bus->updateTrackProperties (props);
        auto v = [&] (const juce::String& id) { return bus->apvts.getRawParameterValue (id)->load(); };
        check (bus->glueRoomTick() && v (Params::Verb::decay) < 1.0f && v (Params::echoLevel) < 0.01f && v (Params::throwLevel) < 0.01f
                   && v (Params::autoSpace) > 0.5f, "a new Antioch on the music bus becomes a short shared room: no echo, no throws, its level set by itself");
        check (! bus->glueRoomTick(), "once");
        auto lead = make();
        lead->captureFreshValues();
        props.name = "Lead";
        lead->updateTrackProperties (props);
        check (! lead->glueRoomTick() && lead->apvts.getRawParameterValue (Params::echoLevel)->load() > 0.4f, "a lead's Antioch stays as it is");
    }

    section ("Throws wait for the lead's phrase");
    {
        auto p = make();
        render (*p, 0.2, [] (juce::AudioBuffer<float>&, int64_t) {}, nullptr, nullptr, [] (int64_t) { suite::ActivityBus::write (suite::ActivityBus::lead, 1.0f); });
        check (p->throwsHeld.load(), "while the lead plays, automatic throws on other tracks wait");
        suite::ActivityBus::clear();
        render (*p, 0.2, [] (juce::AudioBuffer<float>&, int64_t) {});
        check (! p->throwsHeld.load(), "and go again once it stops");
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

    section ("The problem watcher: mud from the reverb returns");
    {
        suite::FixBus::reset();
        auto p = std::make_unique<AntiochProcessor>();
        const auto before = p->apvts.getRawParameterValue (Params::Verb::lowCut)->load();
        const auto seq = suite::FixBus::send (suite::Fix::ReverbLowCut);
        p->fixes->poll();
        const auto after = p->apvts.getRawParameterValue (Params::Verb::lowCut)->load();
        check (after > before * 1.3f && after < before * 1.5f, "the reverb's low cut half an octave up (" + juce::String (before, 0) + " -> " + juce::String (after, 0) + " Hz)");
        suite::FixBus::sendUndo (seq);
        p->fixes->poll();
        check (std::abs (p->apvts.getRawParameterValue (Params::Verb::lowCut)->load() - before) < 1.0f, "undone: back where it was");
        suite::FixBus::reset();
    }


    section ("Mixing helpers: the space at the lead's note, in time with the song");
    {
        suite::Autopilot::reset();
        auto p = make();
        FakePlayHead head; head.bpm = 120.0;
        p->setPlayHead (&head);
        render (*p, 0.5, {}, nullptr, &head);
        check (p->timedNow.load(), "pre-delay and decay follow the tempo");
        render (*p, 1.0, {}, nullptr, &head, [] (int64_t) { suite::Autopilot::publishLeadPitch (440.0f); suite::ActivityBus::write (suite::ActivityBus::lead, 1.0f); });
        check (p->noteRoomNow.load() > 0.5f, "while the lead plays, the tail makes room at its note (" + juce::String (p->noteRoomNow.load(), 2) + ")");
        p->setPlayHead (nullptr);
        suite::Autopilot::reset();
        suite::ActivityBus::clear();
    }


    section ("Level-matched space");
    {
        suite::Autopilot::reset();
        auto run = [] (bool on)
        {
            suite::Autopilot::set (suite::Autopilot::stability, on);
            auto p = make();
            soloSection (*p, 0.0f, 1.0f, 0.0f);
            set (*p, Params::dry, 1.0f);   // the track, with a big reverb on top
            set (*p, Params::Verb::duck, 0.0f);
            set (*p, Params::Verb::decay, 12.0f);
            set (*p, Params::Verb::size, 1.0f);
            juce::Random rng (4);
            juce::AudioBuffer<float> out;
            render (*p, 6.0, [&rng] (juce::AudioBuffer<float>& b, int64_t)
            {
                for (int i = 0; i < b.getNumSamples(); ++i) { const auto v = 0.2f * (rng.nextFloat() * 2.0f - 1.0f); b.setSample (0, i, v); b.setSample (1, i, v); }
            }, &out);
            return juce::Decibels::gainToDecibels (rmsBetween (out, 3.0, 6.0) / (0.2f / std::sqrt (3.0f)));
        };
        const auto off = run (false), on = run (true);
        suite::Autopilot::reset();
        check (off - on > 0.5f && on < 0.3f, "the reverb on top no longer lifts the track over its input (" + juce::String (off, 1) + " dB without, " + juce::String (on, 1) + " dB with)");
    }


    section ("Round B: the space de-essed, a clean ending");
    {
        suite::Autopilot::reset();
        {
            auto p = make();
            soloSection (*p, 0.0f, 1.0f, 0.0f);
            auto essFilter = std::make_shared<suite::Svf>();
            essFilter->set (7000.0f, 2.0f, (float) sampleRate);
            juce::AudioBuffer<float> out;
            render (*p, 4.0, [essFilter] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                juce::Random rng (pos + 3);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto t = (double) (pos + i) / sampleRate;
                    const auto ess = std::fmod (t, 0.5) < 0.08 ? 0.6f * essFilter->bandpass (rng.nextFloat() * 2.0f - 1.0f) : 0.0f;
                    const auto v = 0.15f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * t) + ess;
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            }, &out);
            check (suite::Autopilot::countOf (suite::Autopilot::spaceDeessed) > 0, "an 's' spiking at 7 kHz is de-essed on its way into the space");
        }
        {
            suite::SectionMap map;
            map.totalBars = 2; map.count = 1;
            map.sections[0] = { 1, suite::SectionType::Drop };
            suite::SectionBus::publish (map);
            auto p = make();
            set (*p, Params::Verb::duck, 0.0f);
            FakePlayHead head; head.bpm = 120.0;   // 2 s a bar: the song ends at 4 s
            p->setPlayHead (&head);
            juce::AudioBuffer<float> out;
            render (*p, 9.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
            {
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto t = (double) (pos + i) / sampleRate;
                    const auto v = t < 4.0 ? 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 330.0 * t) : 0.0f;
                    b.setSample (0, i, v); b.setSample (1, i, v);
                }
            }, &out, &head);
            p->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            check (rmsBetween (out, 4.2, 4.6) > 1.0e-3f && rmsBetween (out, 8.1, 9.0) < 2.0e-4f && rmsBetween (out, 6.0, 6.5) < 0.2f * rmsBetween (out, 4.2, 4.6), "after the last bar the tails ring on, then fade out over 2 bars (to -80 dB: "
                       + juce::String (rmsBetween (out, 4.2, 4.6), 5) + ", " + juce::String (rmsBetween (out, 6.0, 6.5), 5) + ", " + juce::String (rmsBetween (out, 8.1, 9.0), 6) + ")");
        }
        suite::Autopilot::reset();
    }

    return finish();
}
