// Offline tests for Antioch. Usage: AntiochTests [output-dir]

#include "PluginProcessor.h"
#include "SuiteUI.h"
#include "TestUtils.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<AntiochProcessor> make()
    {
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
        const char* names[] = { "antioch_echo.png", "antioch_reverb.png", "antioch_throw.png" };
        for (int i = 0; i < 3; ++i)
        {
            if (tabs != nullptr) tabs->setSelected (i);
            snapshot (*editor, outDir.getChildFile (names[i]));
        }
        editor.reset();
        std::cout << "Wrote renders and UI snapshots to " << outDir.getFullPathName() << std::endl;
    }

    return finish();
}
