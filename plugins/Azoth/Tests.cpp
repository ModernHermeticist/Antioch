// Offline tests for Azoth. Usage: AzothTests [output-dir]

#include "PluginProcessor.h"
#include "KeyBus.h"
#include "TestUtils.h"
#include "ChordBus.h"

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

    std::unique_ptr<AzothProcessor> quiet()
    {
        auto p = make();
        for (const auto& id : { AzothProcessor::impactId, AzothProcessor::crashId, AzothProcessor::swellId, AzothProcessor::downId,
                                AzothProcessor::rollId, AzothProcessor::riserId })
            set (*p, id, 0.0f);
        set (*p, AzothProcessor::grooveCrashId, 0.0f);
        return p;
    }

    InputFn sine (float hz, float level = 0.3f)
    {
        return [hz, level] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (pos + i) / sampleRate);
                for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, v);
            }
        };
    }

    float peakFrequency (const juce::AudioBuffer<float>& a, float lo, float hi, double t0, double t1)
    {
        float best = 0.0f, hz = lo;
        for (float f = lo; f <= hi; f += 1.0f)
            if (const auto l = bandLevel (a, f, t0, t1); l > best) { best = l; hz = f; }
        return hz;
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

    section ("Final-drop key change");
    {
        auto m = testMap();   // one drop, so it's the last: +2 semitones
        m.finalLift = 2;
        suite::SectionBus::publish (m);
        auto p = make();
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 13.0, {}, &audio, &head);
        p->setPlayHead (nullptr);
        const auto b = bandLevel (audio, 61.74f, 12.15, 12.65), a = bandLevel (audio, 55.0f, 12.15, 12.65);
        check (b > 0.1f && b > 2.0f * a, "the impact into the lifted drop lands on B1, two semitones up (" + juce::String (b, 3) + " vs "
                   + juce::String (a, 3) + " at A)");
        suite::SectionBus::publish (testMap());
    }

    section ("Energy curve");
    {
        auto impactAt = [] (int dropEnergy)
        {
            auto m = testMap();
            m.sections[3].energy = dropEnergy;
            suite::SectionBus::publish (m);
            auto p = make();
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> audio;
            render (*p, 13.0, {}, &audio, &head);
            p->setPlayHead (nullptr);
            return bandLevel (audio, 55.0f, 12.15, 12.65);
        };
        const auto usual = impactAt (-1), half = impactAt (50);
        check (half > 0.4f * usual && half < 0.6f * usual, "a drop drawn at half energy gets half the impact ("
                   + juce::String (half, 3) + " vs " + juce::String (usual, 3) + ")");
        suite::SectionBus::publish (testMap());
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

    section ("Key-change pre-lift");
    {
        auto m = testMap();   // one drop, so it's the last: +2 semitones
        m.finalLift = 2;
        suite::SectionBus::publish (m);
        FakePlayHead head;
        head.bpm = 120.0;
        juce::AudioBuffer<float> audio;
        auto p = quiet();
        p->setPlayHead (&head);
        render (*p, 12.6, sine (440.0f), &audio, &head);
        p->setPlayHead (nullptr);
        const auto before = peakFrequency (audio, 400.0f, 520.0f, 11.0, 11.4), lifted = peakFrequency (audio, 400.0f, 520.0f, 11.88, 11.99);
        check (std::abs (before - 440.0f) < 3.0f && lifted > 470.0f, "the track glides up towards the new key over the last beat ("
                   + juce::String (before, 0) + " -> " + juce::String (lifted, 0) + " Hz; B is 494)");
        check (std::abs (bandLevel (audio, 440.0f, 12.2, 12.6) - 0.3f) < 0.02f, "and is itself again in the drop");

        head.ppq = 0.0;
        auto q = quiet();
        set (*q, AzothProcessor::preLiftId, 0.0f);
        q->setPlayHead (&head);
        render (*q, 12.6, sine (440.0f), &audio, &head);
        q->setPlayHead (nullptr);
        check (std::abs (peakFrequency (audio, 400.0f, 520.0f, 11.88, 11.99) - 440.0f) < 3.0f, "off: untouched");

        head.ppq = 0.0;
        suite::SectionBus::publish (testMap());   // no key change: nothing to announce
        auto r = quiet();
        r->setPlayHead (&head);
        render (*r, 12.6, sine (440.0f), &audio, &head);
        r->setPlayHead (nullptr);
        check (std::abs (peakFrequency (audio, 400.0f, 520.0f, 11.88, 11.99) - 440.0f) < 3.0f, "without a key change it does nothing");
    }

    section ("Into the drop: tape stop and stutter");
    {
        suite::SectionBus::publish (testMap());
        FakePlayHead head;
        head.bpm = 120.0;
        juce::AudioBuffer<float> audio;

        auto p = quiet();
        set (*p, AzothProcessor::dropFxId, (float) AzothProcessor::dfTapeStop);   // 1 beat: 11.5-12 s
        p->setPlayHead (&head);
        render (*p, 12.6, sine (440.0f), &audio, &head);
        p->setPlayHead (nullptr);
        check (std::abs (bandLevel (audio, 440.0f, 11.0, 11.4) - 0.3f) < 0.01f, "the track plays normally until the last beat");
        const auto slowed = peakFrequency (audio, 60.0f, 500.0f, 11.75, 11.9);
        check (slowed < 250.0f && rmsBetween (audio, 11.75, 11.9) > 0.05f, "then slows down like a stopping tape (" + juce::String (slowed, 0) + " Hz in the last half beat)");
        check (std::abs (bandLevel (audio, 440.0f, 12.1, 12.5) - 0.3f) < 0.01f && p->dropFxRuns.load() == 1, "and comes back at full speed on the drop");

        head.ppq = 0.0;
        auto q = quiet();
        set (*q, AzothProcessor::dropFxId, (float) AzothProcessor::dfTapeStop);
        set (*q, AzothProcessor::gapId, 2.0f);   // 1 beat gap: the stop runs 11.0-11.5 s, then silence
        q->setPlayHead (&head);
        render (*q, 12.6, sine (440.0f), &audio, &head);
        q->setPlayHead (nullptr);
        check (peakBetween (audio, 11.52, 11.98) < 1.0e-6f && rmsBetween (audio, 11.2, 11.3) > 0.05f, "with a gap, the tape stops before it and stays stopped");

        head.ppq = 0.0;
        auto r = quiet();
        set (*r, AzothProcessor::dropFxId, (float) AzothProcessor::dfStutter);
        set (*r, AzothProcessor::dropFxLenId, 2.0f);   // 2 beats: 11-12 s, 1/8 slices (0.25 s) for the first beat
        r->setPlayHead (&head);
        auto chirp = [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * (200.0 + 40.0 * t) * t);
                for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, v);
            }
        };
        render (*r, 12.6, chirp, &audio, &head);
        r->setPlayHead (nullptr);
        float diff = 0.0f;
        const auto slice = (int) (0.25 * sampleRate);
        for (int i = (int) (11.02 * sampleRate); i < (int) (11.2 * sampleRate); ++i)
            diff = std::max (diff, std::abs (audio.getSample (0, i) - audio.getSample (0, i + slice)));
        check (diff < 1.0e-4f && rmsBetween (audio, 11.02, 11.2) > 0.1f, "stutter repeats the same 1/8 slice (max difference " + juce::String (diff, 6) + ")");
    }

    section ("Build shape and build filter");
    {
        suite::SectionBus::publish (testMap());
        FakePlayHead head;
        head.bpm = 120.0;
        juce::AudioBuffer<float> audio;
        auto riserPitches = [&] (int shape)
        {
            auto p = quiet();
            set (*p, AzothProcessor::riserId, 1.0f);
            set (*p, AzothProcessor::buildShapeId, (float) shape);
            head.ppq = 0.0;
            p->setPlayHead (&head);
            render (*p, 10.0, {}, &audio, &head);
            p->setPlayHead (nullptr);
            return std::make_pair (peakFrequency (audio, 200.0f, 330.0f, 8.1, 8.45), peakFrequency (audio, 200.0f, 330.0f, 8.55, 8.9));
        };
        const auto smooth = riserPitches (AzothProcessor::bsSmooth), stepped = riserPitches (AzothProcessor::bsStepped);
        check (smooth.second > smooth.first + 5.0f, "smooth: the riser glides up (" + juce::String (smooth.first, 0) + " -> " + juce::String (smooth.second, 0) + " Hz)");
        check (std::abs (stepped.second - stepped.first) < 4.0f && stepped.first > smooth.first + 8.0f,
               "stepped: it jumps straight to its first stage and holds it (" + juce::String (stepped.first, 0) + " / " + juce::String (stepped.second, 0) + " Hz)");

        auto p = quiet();
        set (*p, AzothProcessor::rollId, 0.6f);
        set (*p, AzothProcessor::buildShapeId, (float) AzothProcessor::bsLateSurge);
        head.ppq = 0.0;
        p->setPlayHead (&head);
        render (*p, 12.5, {}, &audio, &head);
        p->setPlayHead (nullptr);
        check (p->snareHits.load() < 26, "late surge keeps the roll slow for longer and speeds up only at the end: " + juce::String (p->snareHits.load()) + " hits (smooth: 30)");

        auto f = quiet();
        set (*f, AzothProcessor::buildFilterId, 1.0f);
        head.ppq = 0.0;
        f->setPlayHead (&head);
        render (*f, 12.8, sine (100.0f), &audio, &head);
        f->setPlayHead (nullptr);
        const auto before = bandLevel (audio, 100.0f, 6.0, 7.0), late = bandLevel (audio, 100.0f, 11.5, 11.9), after = bandLevel (audio, 100.0f, 12.3, 12.7);
        check (std::abs (before - 0.3f) < 0.01f && late < 0.2f * before && after > 0.9f * before,
               "the build filter thins the track out across the build and opens on the drop (" + juce::String (before, 3) + " -> "
                   + juce::String (late, 3) + " -> " + juce::String (after, 3) + ")");
    }

    section ("Your own samples");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AzothSamples");
        dir.deleteRecursively();
        dir.createDirectory();
        const auto wav = dir.getChildFile ("my impact.wav");
        {
            juce::AudioBuffer<float> b (1, 22050);   // 0.5 s of 1 kHz at 44.1 kHz (played back at 48 kHz)
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 44100.0));
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (wav), 44100.0, 1, 24, {}, 0));
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }

        suite::SectionBus::publish (testMap());
        auto p = quiet();
        set (*p, AzothProcessor::impactId, 1.0f);
        check (p->loadSample (AzothProcessor::slotImpact, wav) && p->sampleName (AzothProcessor::slotImpact) == "my impact", "loads a WAV as the impact");
        FakePlayHead head;
        head.bpm = 120.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 12.6, {}, &audio, &head);
        p->setPlayHead (nullptr);
        const auto mine = bandLevel (audio, 1000.0f, 12.05, 12.4), sub = bandLevel (audio, 55.0f, 12.05, 12.4);
        check (mine > 0.4f && sub < 0.02f, "the drop plays your sample, at its own pitch, instead of the sub boom (" + juce::String (mine, 3) + " at 1 kHz, "
                   + juce::String (sub, 3) + " at 55 Hz)");
        check (rmsBetween (audio, 12.55, 12.6) < 1.0e-4f, "and stops when the sample ends");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->sampleName (AzothProcessor::slotImpact) == "my impact", "the sample is remembered with the project");
        q->clearSample (AzothProcessor::slotImpact);
        check (q->sampleName (AzothProcessor::slotImpact).isEmpty(), "and can go back to the synthesised one");
        check (! p->loadSample (AzothProcessor::slotCrash, dir.getChildFile ("missing.wav")), "a missing file is refused");
        dir.deleteRecursively();
    }

    section ("Your samples in key, level-matched, and your tag");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AzothTuned");
        dir.deleteRecursively();
        dir.createDirectory();
        auto writeTone = [&] (const char* name, float hz, float amp, double seconds)
        {
            const auto f = dir.getChildFile (name);
            juce::AudioBuffer<float> b (1, (int) (seconds * 48000.0));
            for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / 48000.0));
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (new juce::FileOutputStream (f), 48000.0, 1, 24, {}, 0));
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            return f;
        };
        const auto boom = writeTone ("quiet boom.wav", 100.0f, 0.05f, 0.6);   // between G# and A, and very quiet
        auto run = [&] (bool tune)
        {
            suite::SectionBus::publish (testMap());   // A minor; the drop at 12 s
            auto p = quiet();
            set (*p, AzothProcessor::impactId, 1.0f);
            set (*p, AzothProcessor::tuneSamplesId, tune ? 1.0f : 0.0f);
            p->loadSample (AzothProcessor::slotImpact, boom);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> audio;
            render (*p, 12.6, {}, &audio, &head);
            p->setPlayHead (nullptr);
            return std::make_pair (peakFrequency (audio, 80.0f, 130.0f, 12.05, 12.5), rmsBetween (audio, 12.05, 12.5));
        };
        auto q = quiet();
        q->loadSample (AzothProcessor::slotImpact, boom);
        check (std::abs (q->samplePitch (AzothProcessor::slotImpact) - 100.0f) < 2.0f, "finds the sample's pitch (" + juce::String (q->samplePitch (AzothProcessor::slotImpact), 1) + " Hz)");
        const auto [tunedHz, tunedRms] = run (true);
        const auto [plainHz, plainRms] = run (false);
        check (std::abs (tunedHz - 110.0f) < 2.5f && std::abs (plainHz - 100.0f) < 2.5f, "tuned to the song's A (" + juce::String (tunedHz, 0) + " Hz; untuned "
                   + juce::String (plainHz, 0) + " Hz)");
        check (tunedRms > 0.2f, "and level-matched: a sample at -26 dBFS plays like the built-in impact (rms " + juce::String (tunedRms, 3) + ")");

        // The tag: on the first drop only, or on every drop.
        suite::SectionMap twoDrops;
        twoDrops.totalBars = 8; twoDrops.count = 3;
        twoDrops.sections[0] = { 1, T::Drop };       // 0-4 s
        twoDrops.sections[1] = { 3, T::Breakdown };  // 4-8 s
        twoDrops.sections[2] = { 5, T::Drop };       // 8-16 s
        const auto tag = writeTone ("my tag.wav", 1000.0f, 0.3f, 0.4);
        auto tags = [&] (int where)
        {
            suite::SectionMap withIntro = twoDrops;   // start in a short groove so the first drop is entered
            withIntro.count = 4; withIntro.totalBars = 9;
            withIntro.sections[0] = { 1, T::Groove };
            withIntro.sections[1] = { 2, T::Drop };       // 2-6 s
            withIntro.sections[2] = { 4, T::Breakdown };  // 6-10 s
            withIntro.sections[3] = { 6, T::Drop };       // 10-18 s
            suite::SectionBus::publish (withIntro);
            auto p = quiet();
            p->loadSample (AzothProcessor::slotTag, tag);
            set (*p, AzothProcessor::tagWhereId, (float) where);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> audio;
            render (*p, 10.6, {}, &audio, &head);
            p->setPlayHead (nullptr);
            return std::make_tuple (p->tagsPlayed.load(), bandLevel (audio, 1000.0f, 2.05, 2.3), bandLevel (audio, 1000.0f, 10.05, 10.3));
        };
        const auto [firstCount, firstA, firstB] = tags (AzothProcessor::tagFirstDrop);
        const auto [everyCount, everyA, everyB] = tags (AzothProcessor::tagEveryDrop);
        const auto [offCount, offA, offB] = tags (AzothProcessor::tagOff);
        check (firstCount == 1 && firstA > 0.1f && firstB < 0.01f, "your tag plays on the first drop only");
        check (everyCount == 2 && everyB > 0.1f && offCount == 0 && offA < 0.01f, "or on every drop, or not at all");
        suite::SectionBus::publish (testMap());
        dir.deleteRecursively();
    }

    section ("Riser follows the chords; roll designer");
    {
        check (AzothProcessor::rollRate (0, 1, AzothProcessor::rcSteps, 0.1) == 1.0 && AzothProcessor::rollRate (0, 1, AzothProcessor::rcSteps, 0.6) == 0.25
                   && AzothProcessor::rollRate (0, 1, AzothProcessor::rcSteps, 0.9) == 0.125, "the default roll is the classic 1/4, 1/8, 1/16, 1/32 in four steps");
        check (AzothProcessor::rollRate (1, 2, AzothProcessor::rcSteps, 0.95) == 0.0625 && AzothProcessor::rollRate (1, 2, AzothProcessor::rcSteps, 0.1) == 0.5,
               "from 1/8 to 1/64");
        const auto smoothMid = AzothProcessor::rollRate (0, 1, AzothProcessor::rcSmooth, 0.5);
        check (smoothMid > 0.3 && smoothMid < 0.4 && AzothProcessor::rollRate (0, 1, AzothProcessor::rcLate, 0.6) == 0.5,
               "smooth glides between the rates; late stays slow for longer");

        auto hits = [] (int to)
        {
            suite::SectionBus::publish (testMap());
            auto p = quiet();
            set (*p, AzothProcessor::rollId, 0.6f);
            set (*p, AzothProcessor::rollToId, (float) to);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            render (*p, 12.2, {}, nullptr, &head);
            p->setPlayHead (nullptr);
            return p->snareHits.load();
        };
        const auto to32 = hits (1), to64 = hits (2);
        check (to32 == 30 && to64 > to32, "rolling on to 1/64 adds hits at the end (" + juce::String (to32) + " -> " + juce::String (to64) + ")");

        auto riserHz = [] (bool follow)
        {
            suite::SectionBus::publish (testMap());
            suite::Progression prog;
            prog.root = 9; prog.scale = 0; prog.count = 2; prog.quartersPerChord = 4.0;
            prog.chordRoots[0] = 0; prog.chordRoots[1] = 8;   // i, then VI: up a minor sixth
            suite::ChordBus::publish (prog);
            auto p = quiet();
            set (*p, AzothProcessor::riserId, 1.0f);
            set (*p, AzothProcessor::riserChordsId, follow ? 1.0f : 0.0f);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> audio;
            render (*p, 10.6, {}, &audio, &head);
            p->setPlayHead (nullptr);
            suite::ChordBus::withdraw();
            return std::make_pair (peakFrequency (audio, 180.0f, 700.0f, 8.15, 8.45), peakFrequency (audio, 180.0f, 700.0f, 10.15, 10.45));
        };
        const auto [onFirst, onSecond] = riserHz (true);
        const auto [offFirst, offSecond] = riserHz (false);
        check (std::abs (onFirst - offFirst) < 4.0f && std::abs (onSecond / offSecond - 1.587f) < 0.08f,
               "the riser climbs with the chords: on the VI it's a minor sixth higher (" + juce::String (offSecond, 0) + " -> " + juce::String (onSecond, 0) + " Hz)");
    }

    section ("Sound design presets");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AzothPresets");
        dir.deleteRecursively();
        AzothProcessor::presetFolderOverride() = dir;
        auto p = quiet();
        set (*p, AzothProcessor::rollToId, 2.0f);
        set (*p, AzothProcessor::impactId, 0.33f);
        check (p->savePreset ("Hard drops") && AzothProcessor::presetFiles().size() == 1, "saves every setting as a preset");
        auto q = make();
        check (q->loadPreset (AzothProcessor::presetFiles()[0])
                   && (int) q->apvts.getRawParameterValue (AzothProcessor::rollToId)->load() == 2
                   && std::abs (q->apvts.getRawParameterValue (AzothProcessor::impactId)->load() - 0.33f) < 0.01f,
               "and another project loads it (roll to 1/64, impact 33%)");
        AzothProcessor::presetFolderOverride() = juce::File();
        dir.deleteRecursively();
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
