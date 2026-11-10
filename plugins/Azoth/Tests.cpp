// Offline tests for Azoth. Usage: AzothTests [output-dir]

#include "PluginProcessor.h"
#include "KeyBus.h"
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

    std::unique_ptr<AzothProcessor> make()
    {
        auto p = std::make_unique<AzothProcessor>();
        set (*p, AzothProcessor::levelId, 0.0f);
        prepare (*p);
        return p;
    }

    float bandLevel (const juce::AudioBuffer<float>& a, float hz, double t0, double t1)
    {
        double s = 0.0, c = 0.0;
        const auto s0 = (int) (t0 * sampleRate), s1 = (int) (t1 * sampleRate);
        for (int i = s0; i < s1; ++i)
        {
            const auto w = juce::MathConstants<double>::twoPi * hz * i / sampleRate;
            s += a.getSample (0, i) * std::sin (w);
            c += a.getSample (0, i) * std::cos (w);
        }
        return (float) (2.0 * std::sqrt (s * s + c * c) / (s1 - s0));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    suite::KeyBus::publish (9, 0);   // A minor: subs land on A1 (55 Hz)

    section ("Transitions follow the map");
    {
        suite::SectionBus::publish (testMap());
        auto p = make();
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 16.0, {}, &audio, &head);
        p->setPlayHead (nullptr);

        check (peakBetween (audio, 0.0, 3.95) < 1.0e-4f, "nothing in the groove (no transition at the song start)");
        check (rmsBetween (audio, 4.0, 5.5) > 0.01f, "downlifter at the breakdown (4 s)");
        check (p->snareHits.load() == 30, "snare roll: 2 + 4 + 8 + 16 = 30 hits across the 2-bar build (" + juce::String (p->snareHits.load()) + ")");
        check (rmsBetween (audio, 11.6, 11.95) > rmsBetween (audio, 11.0, 11.35), "the swell rises into the drop");
        const auto sub = bandLevel (audio, 55.0f, 12.15, 12.65), offKey = bandLevel (audio, 73.4f, 12.15, 12.65);
        check (sub > 0.1f && sub > 3.0f * offKey, "impact lands on the root, A1 (" + juce::String (sub, 3) + " at 55 Hz vs "
                                                      + juce::String (offKey, 3) + " at 73 Hz)");
        check (p->eventsFired.load() == 2, "two section transitions fired (breakdown, drop)");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("azoth_transitions.wav"), audio);
    }

    section ("Later drops hit harder");
    {
        suite::SectionMap twoDrops;   // Groove 1, Drop 2, Breakdown 3, Drop 4 (2 s per bar)
        twoDrops.totalBars = 5; twoDrops.count = 4;
        twoDrops.sections[0] = { 1, T::Groove };
        twoDrops.sections[1] = { 2, T::Drop };
        twoDrops.sections[2] = { 3, T::Breakdown };
        twoDrops.sections[3] = { 4, T::Drop };
        suite::SectionBus::publish (twoDrops);
        auto p = make();
        set (*p, AzothProcessor::downId, 0.0f);
        set (*p, AzothProcessor::swellId, 0.0f);
        set (*p, AzothProcessor::crashId, 0.0f);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 8.0, {}, &audio, &head);
        p->setPlayHead (nullptr);
        const auto first = peakBetween (audio, 2.0, 2.4), second = peakBetween (audio, 6.0, 6.4);
        check (second > 1.2f * first, "the second drop's impact is bigger (" + juce::String (first, 2) + " -> " + juce::String (second, 2) + ")");
    }

    section ("Gap before the drop");
    {
        suite::SectionBus::publish (testMap());
        auto p = make();
        set (*p, AzothProcessor::gapId, 2.0f);       // 1 beat of silence before the drop
        set (*p, AzothProcessor::downId, 0.0f);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 13.0, {}, &audio, &head);
        p->setPlayHead (nullptr);
        // The last hit is half a beat before the gap; by 11.8 s its tail has died away.
        // The roll is squeezed into 7 beats (16..23): speed changes at 17.75, 19.5 and 21.25 give 2 + 3 + 7 + 14 hits.
        check (p->snareHits.load() == 26, "the roll ends a beat early: 26 hits, none in the gap (" + juce::String (p->snareHits.load()) + ")");
        check (rmsBetween (audio, 11.8, 11.98) < 0.05f * rmsBetween (audio, 11.0, 11.45), "the last beat before the drop goes quiet");
        check (rmsBetween (audio, 12.02, 12.5) > 0.05f, "then the drop hits");
    }

    section ("Build risers");
    {
        // testMap: Build bars 5-6 = 8-12 s at 120 BPM. Only the riser on.
        auto riserOnly = [] ()
        {
            auto p = make();
            for (const auto& id : { AzothProcessor::impactId, AzothProcessor::crashId, AzothProcessor::swellId, AzothProcessor::downId,
                                    AzothProcessor::rollId })
                set (*p, id, 0.0f);
            set (*p, AzothProcessor::grooveCrashId, 0.0f);
            return p;
        };
        suite::SectionBus::publish (testMap());
        auto p = riserOnly();
        set (*p, AzothProcessor::riserId, 1.0f);
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 12.5, {}, &audio, &head);
        p->setPlayHead (nullptr);
        check (rmsBetween (audio, 0.0, 7.95) < 1.0e-6f, "silent before the build");
        check (rmsBetween (audio, 11.0, 11.9) > 4.0f * rmsBetween (audio, 8.4, 9.0), "rises in level through the build");
        float peakHz = 0.0f, peakLevel = 0.0f;
        for (float hz = 180.0f; hz <= 280.0f; hz += 1.0f)
            if (const auto l = bandLevel (audio, hz, 8.1, 8.5); l > peakLevel) { peakLevel = l; peakHz = hz; }
        check (peakHz > 214.0f && peakHz < 227.0f, "starts on the song's root (A3 = 220 Hz, from the key): strongest at " + juce::String (peakHz, 0) + " Hz");
        check (bandLevel (audio, 220.0f, 11.5, 11.9) < 0.5f * bandLevel (audio, 220.0f, 8.6, 9.0) * (rmsBetween (audio, 11.5, 11.9) / rmsBetween (audio, 8.6, 9.0)),
               "has swept up away from the root by the end");
        check (rmsBetween (audio, 12.05, 12.5) < 1.0e-6f, "stops at the drop");

        // Track riser: the track's own audio pitched up through the build.
        auto q = riserOnly();
        set (*q, AzothProcessor::riserId, 0.0f);
        set (*q, AzothProcessor::trackRiserId, 1.0f);
        head.ppq = 0.0;
        q->setPlayHead (&head);
        auto tone = [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) (pos + i) / sampleRate);
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };
        render (*q, 12.5, tone, &audio, &head);
        q->setPlayHead (nullptr);
        // Near the end of the build the shift is ~11 semitones: 440 Hz -> ~830 Hz.
        const auto shifted = bandLevel (audio, 830.0f, 11.6, 11.95), before = bandLevel (audio, 830.0f, 6.0, 7.0);
        check (shifted > 0.05f && before < 0.005f, "the track's audio rises an octave across the build ("
                   + juce::String (shifted, 3) + " at 830 Hz, before " + juce::String (before, 4) + ")");
        check (std::abs (bandLevel (audio, 440.0f, 6.0, 7.0) - 0.3f) < 0.01f, "the track passes through untouched before the build");
        check (bandLevel (audio, 830.0f, 12.6 - 0.5, 12.5) < 0.2f * shifted, "and lets go at the drop");
    }

    section ("Reverse swells from the track's own audio");
    {
        // Groove bars 1-2 (0-4 s), Drop bars 3-4 (4-8 s); a single pluck on the drop's downbeat.
        suite::SectionMap map;
        map.totalBars = 4; map.count = 2;
        map.sections[0] = { 1, T::Groove };
        map.sections[1] = { 3, T::Drop };
        suite::SectionBus::publish (map);

        auto p = std::make_unique<AzothProcessor>();
        set (*p, AzothProcessor::levelId, 0.0f);
        for (auto* id : { &AzothProcessor::impactId, &AzothProcessor::crashId, &AzothProcessor::swellId })
            set (*p, *id, 0.0f);
        set (*p, AzothProcessor::reverseOnId, 1.0f);     // 1 bar swell into drops
        prepare (*p);
        check (p->getLatencySamples() == (int) (3.0 * sampleRate), "reports 3 s of latency (" + juce::String (p->getLatencySamples()) + " samples)");

        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 9.0, pluck (330.0f, 4.0), &audio, &head);
        p->setPlayHead (nullptr);

        // Output is 3 s late: the pluck comes out at 7 s, the 1-bar (2 s) swell occupies 5-7 s.
        check (peakBetween (audio, 6.95, 7.2) > 0.3f, "the track's own audio comes out 3 s later (pluck at 7 s)");
        check (peakBetween (audio, 0.0, 4.95) < 1.0e-4f, "silence before the swell");
        const auto early = rmsBetween (audio, 5.1, 5.6), late = rmsBetween (audio, 6.4, 6.95);
        check (late > 0.001f && late > 2.0f * early, "a swell rises into the downbeat (" + juce::String (early, 5) + " -> " + juce::String (late, 5) + ")");
        check (p->reverseSamplesPlayed() > (int64_t) (1.9 * sampleRate), "the whole 1-bar swell played");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("azoth_reverse_swell.wav"), audio);

        // With the lookahead on, the synth transitions move with the delayed audio.
        auto q = std::make_unique<AzothProcessor>();
        set (*q, AzothProcessor::levelId, 0.0f);
        set (*q, AzothProcessor::reverseOnId, 1.0f);
        set (*q, AzothProcessor::reverseLevelId, 0.0f);
        set (*q, AzothProcessor::crashId, 0.0f);
        set (*q, AzothProcessor::swellId, 0.0f);
        prepare (*q);
        FakePlayHead head2;
        head2.bpm = 120.0;
        q->setPlayHead (&head2);
        render (*q, 8.0, {}, &audio, &head2);
        q->setPlayHead (nullptr);
        check (peakBetween (audio, 3.9, 6.95) < 1.0e-4f && peakBetween (audio, 7.0, 7.5) > 0.1f,
               "the impact lands at 7 s, in line with the delayed audio");
        suite::SectionBus::withdraw();
    }

    section ("Fix-it from Mentalism");
    {
        auto p = make();
        auto v = [&p] (const juce::String& id) { return p->apvts.getParameter (id)->getValue(); };
        const auto swell = v (AzothProcessor::swellId), later = v (AzothProcessor::laterDropsId);
        suite::FixBus::send (suite::Fix::BuildRise);
        p->fixes->poll();
        check (std::abs (v (AzothProcessor::swellId) - swell - 0.1f) < 0.01f, "a flat build gets more swell");
        suite::FixBus::send (suite::Fix::LaterDropBigger);
        p->fixes->poll();
        check (std::abs (v (AzothProcessor::laterDropsId) - later - 0.25f) < 0.01f, "a weaker later drop gets 'Bigger later drops' turned up");
    }

    section ("No surprises");
    {
        suite::SectionBus::publish (testMap());
        auto p = make();
        FakePlayHead head;
        head.bpm = 120.0;
        head.ppq = 4.0 * 6.5;        // jump straight into the middle of the drop
        p->setPlayHead (&head);
        render (*p, 1.0, {}, nullptr, &head);
        check (p->eventsFired.load() == 0, "jumping into the drop doesn't fire an impact");

        set (*p, AzothProcessor::followId, 0.0f);
        head.ppq = 0.0;
        juce::AudioBuffer<float> audio;
        render (*p, 13.0, {}, &audio, &head);
        p->setPlayHead (nullptr);
        check (peakBetween (audio, 0.0, 13.0) < 1.0e-6f, "Follow off: silent");

        p->audition (AzothProcessor::aImpact);
        render (*p, 0.5, {}, &audio);
        check (peakBetween (audio, 0.0, 0.5) > 0.1f, "audition plays even without the map");

        auto q = make();
        set (*q, AzothProcessor::followId, 0.0f);
        render (*q, 0.2, pluck(), &audio);
        juce::AudioBuffer<float> ref (2, audio.getNumSamples()); ref.clear(); pluck() (ref, 0);
        float diff = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i) diff = std::max (diff, std::abs (audio.getSample (0, i) - ref.getSample (0, i)));
        check (diff < 1.0e-6f, "the track's own audio passes through");
    }

    if (outDir != juce::File())
    {
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        snapshot (*editor, outDir.getChildFile ("azoth.png"));
        editor.reset();
    }

    suite::SectionBus::withdraw();
    suite::KeyBus::withdraw();
    return finish();
}
