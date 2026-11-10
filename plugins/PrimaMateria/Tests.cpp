// Offline tests for Prima Materia. Usage: PrimaMateriaTests [output-dir]

#include "PluginProcessor.h"
#include "TestUtils.h"

using namespace suite::test;
using T = suite::SectionType;

namespace
{
    // 120 BPM, 2 s per bar: Groove 1-2 (0-4 s), Breakdown 3-4 (4-8 s), Build 5-6 (8-12 s), Drop 7-8 (12-16 s).
    suite::SectionMap testMap()
    {
        suite::SectionMap m;
        m.totalBars = 8; m.count = 4;
        m.sections[0] = { 1, T::Groove };
        m.sections[1] = { 3, T::Breakdown };
        m.sections[2] = { 5, T::Build };
        m.sections[3] = { 7, T::Drop };
        return m;
    }

    InputFn tone (float hz)
    {
        return [hz] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
    }

    juce::AudioBuffer<float> play (PrimaMateriaProcessor& p, InputFn input, double seconds = 16.0)
    {
        suite::SectionBus::publish (testMap());
        FakePlayHead head;
        head.bpm = 120.0;
        p.setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (p, seconds, input, &audio, &head);
        p.setPlayHead (nullptr);
        return audio;
    }

    std::unique_ptr<PrimaMateriaProcessor> make (int role)
    {
        auto p = std::make_unique<PrimaMateriaProcessor>();
        p->applyRole (role);
        prepare (*p);
        return p;
    }

    int roleIndex (const char* name)
    {
        for (size_t i = 0; i < pm::roles().size(); ++i)
            if (juce::String (pm::roles()[i].name).startsWith (name)) return (int) i;
        return 0;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    section ("Kick role: out in the breakdown and build, back for the drop");
    {
        auto p = make (roleIndex ("Kick"));
        const auto a = play (*p, tone (60.0f));
        check (rmsBetween (a, 1.0, 3.5) > 0.3f, "plays in the groove");
        check (rmsBetween (a, 4.3, 11.7) < 1.0e-3f, "silent through breakdown and build");
        check (rmsBetween (a, 12.3, 15.5) > 0.3f, "back in the drop");
    }

    section ("Bass role: fades back in thin through the build, gap, full at the drop");
    {
        auto p = make (roleIndex ("Bass"));
        const auto a = play (*p, tone (80.0f));
        const auto early = rmsBetween (a, 8.2, 8.8), late = rmsBetween (a, 11.0, 11.4);
        check (rmsBetween (a, 4.3, 7.7) < 1.0e-3f, "out in the breakdown");
        const auto drop = rmsBetween (a, 12.3, 15.5);
        check (late > 1.5f * early, "fades in across the build (" + juce::String (early, 4) + " -> " + juce::String (late, 4) + ")");
        check (late < 0.1f * drop, "but stays thin (400 Hz low cut) until the drop");
        check (rmsBetween (a, 11.55, 11.95) < 1.0e-3f, "a beat of silence before the drop");
        check (rmsBetween (a, 12.3, 15.5) > 0.3f, "full in the drop");
    }

    section ("Pads role: high cut and level per section");
    {
        auto p = make (roleIndex ("Pads"));
        const auto a = play (*p, tone (440.0f));
        const auto breakdown = rmsBetween (a, 5.0, 7.5), drop = rmsBetween (a, 13.0, 15.5);
        const auto diff = juce::Decibels::gainToDecibels (breakdown / drop);
        check (std::abs (diff - 4.0f) < 0.5f, "breakdown +2 dB vs drop -2 dB (" + juce::String (diff, 2) + " dB apart)");
    }

    section ("Morph glides, instant switches");
    {
        auto p = make (roleIndex ("Kick"));
        set (*p, pm::morphId, 3.0f);   // 1 bar glide
        const auto a = play (*p, tone (60.0f), 6.0);
        check (rmsBetween (a, 4.05, 4.25) > 0.05f, "with a 1-bar Morph the kick fades out rather than cutting");
        auto q = make (roleIndex ("Kick"));
        set (*q, pm::morphId, 0.0f);
        const auto b = play (*q, tone (60.0f), 6.0);
        check (rmsBetween (b, 4.02, 4.25) < 1.0e-3f, "Instant cuts on the bar line");
    }

    section ("Pass-through when not following");
    {
        auto p = make (roleIndex ("Kick"));
        set (*p, pm::followId, 0.0f);
        const auto a = play (*p, tone (60.0f), 6.0);
        check (rmsBetween (a, 4.3, 5.5) > 0.3f, "Follow off: unchanged");
        suite::SectionBus::withdraw();
        auto q = make (roleIndex ("Kick"));
        juce::AudioBuffer<float> b;
        render (*q, 1.0, tone (60.0f), &b);
        check (std::abs (rmsBetween (b, 0.2, 0.9) - 0.5f / std::sqrt (2.0f)) < 0.01f, "no map: unchanged");
    }

    if (outDir != juce::File())
    {
        auto p = make (roleIndex ("Pads"));
        play (*p, tone (220.0f), 10.0);   // stop inside the build so a column is lit
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("prima_materia.png"));
        editor.reset();
    }

    suite::SectionBus::withdraw();
    return finish();
}
