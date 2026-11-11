// Offline tests for Kybalion. Usage: KybalionTests [output-dir]

#include "PluginProcessor.h"
#include "SuiteUI.h"
#include "SectionBus.h"
#include "TestUtils.h"
#include "Registry.h"
#include "KickBus.h"

using namespace suite::test;

namespace
{
    std::unique_ptr<KybalionProcessor> make (bool gate, bool duck)
    {
        suite::SectionBus::withdraw();   // no leftover arrangement from another test
        auto p = std::make_unique<KybalionProcessor>();
        set (*p, Params::gateOn, gate ? 1.0f : 0.0f);
        set (*p, Params::duckOn, duck ? 1.0f : 0.0f);
        prepare (*p);
        return p;
    }

    InputFn tone (float hz, float amp = 0.5f)
    {
        return [hz, amp] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) (pos + i) / sampleRate);
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
        };
    }

    float channelRms (const juce::AudioBuffer<float>& b, int ch, double t0, double t1)
    {
        const auto s0 = (int) (t0 * sampleRate), s1 = (int) (t1 * sampleRate);
        return b.getRMSLevel (ch, s0, s1 - s0);
    }

    // Level of a sine of `hz` in a window (correlation), to measure one band in a mix.
    float bandLevel (const juce::AudioBuffer<float>& a, float hz, double t0, double t1)
    {
        double s = 0.0, c = 0.0;
        const auto s0 = (int) (t0 * sampleRate), s1 = (int) (t1 * sampleRate);
        for (int i = s0; i < s1; ++i)
        {
            const auto w = 2.0 * juce::MathConstants<double>::pi * hz * i / sampleRate;
            s += a.getSample (0, i) * std::sin (w);
            c += a.getSample (0, i) * std::cos (w);
        }
        return (float) (2.0 * std::sqrt (s * s + c * c) / (s1 - s0));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    // Never the real style log (Kybalion logs the gate patterns you keep there).
    const auto testStyleLog = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KybalionTestsStyleLog.txt");
    testStyleLog.deleteFile();
    suite::StyleLog::setFileForTests (testStyleLog);
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File())
        outDir.createDirectory();

    FakePlayHead head;
    head.bpm = 120.0; // quarter = 500 ms, 16th step = 125 ms

    section ("Bypass: transparent with both sections off");
    {
        auto p = make (false, false);
        juce::AudioBuffer<float> audio, ref;
        render (*p, 0.5, tone (440.0f), &audio);
        ref.setSize (2, audio.getNumSamples()); ref.clear(); tone (440.0f) (ref, 0);
        float diff = 0.0f;
        for (int i = 0; i < audio.getNumSamples(); ++i) diff = std::max (diff, std::abs (audio.getSample (0, i) - ref.getSample (0, i)));
        check (diff < 1.0e-5f, "output equals input");
    }

    section ("Rhythm: steps open and close on the 16th grid");
    {
        auto p = make (true, false);
        for (int i = 0; i < Params::numSteps; ++i) set (*p, Params::Gate::step (i), i % 2 == 0 ? 1.0f : 0.0f);
        set (*p, Params::Gate::length, 1.0f);
        head.ppq = 0.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 1.0, tone (440.0f), &audio, &head);
        const auto on = channelRms (audio, 0, 0.03, 0.11), off = channelRms (audio, 0, 0.18, 0.245); // after the 15 ms release
        check (on > 0.3f, "step 1 (on) passes audio (" + juce::String (on, 3) + ")");
        check (off < 0.01f, "step 2 (off) is silent (" + juce::String (off, 4) + ")");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("kybalion_gate.wav"), audio);

        set (*p, Params::Gate::stereo, 1.0f);
        head.ppq = 0.0;
        render (*p, 0.3, tone (440.0f), &audio, &head);
        check (channelRms (audio, 0, 0.03, 0.11) > 0.3f && channelRms (audio, 1, 0.03, 0.11) < 0.01f,
               "stereo offset: right channel plays the opposite step");

        set (*p, Params::Gate::stereo, 0.0f);
        set (*p, Params::Gate::swing, 0.5f);   // first 16th of each pair stretches to 187.5 ms
        head.ppq = 0.0;
        render (*p, 0.3, tone (440.0f), &audio, &head);
        check (channelRms (audio, 0, 0.14, 0.18) > 0.3f, "swing delays the off-step (still open at 160 ms)");

        set (*p, Params::Gate::swing, 0.0f);
        for (int i = 0; i < Params::numSteps; ++i) set (*p, Params::Gate::step (i), 1.0f);
        set (*p, Params::Gate::pan, (float) Params::Gate::PanPingPong);
        head.ppq = 0.0;
        render (*p, 0.3, tone (440.0f), &audio, &head);
        const auto l1 = channelRms (audio, 0, 0.03, 0.11), r1 = channelRms (audio, 1, 0.03, 0.11);
        const auto l2 = channelRms (audio, 0, 0.16, 0.24), r2 = channelRms (audio, 1, 0.16, 0.24);
        check (l1 > 0.34f && r1 < 0.1f && r2 > 0.34f && l2 < 0.1f,
               "pan steps (ping-pong): step 1 on the left, step 2 on the right (" + juce::String (l1, 2) + "/" + juce::String (r1, 2)
               + ", " + juce::String (l2, 2) + "/" + juce::String (r2, 2) + ")");
        set (*p, Params::Gate::pan, 0.0f);
        p->setPlayHead (nullptr);
    }

    section ("Rhythm: filter darkens closed steps");
    {
        auto p = make (true, false);
        for (int i = 0; i < Params::numSteps; ++i) set (*p, Params::Gate::step (i), i % 2 == 0 ? 1.0f : 0.0f);
        set (*p, Params::Gate::length, 1.0f);
        set (*p, Params::Gate::depth, 0.0f);   // no volume gating, only the filter
        set (*p, Params::Gate::filter, 1.0f);
        head.ppq = 0.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 0.3, tone (5000.0f), &audio, &head);
        p->setPlayHead (nullptr);
        const auto open = channelRms (audio, 0, 0.03, 0.11), closed = channelRms (audio, 0, 0.17, 0.235);
        check (closed < 0.25f * open, "5 kHz is filtered on off-steps (" + juce::String (juce::Decibels::gainToDecibels (closed / open), 1) + " dB)");
    }

    section ("Polarity: ducks on every beat, recovers before the next");
    {
        auto p = make (false, true);
        p->applyDuckPreset (0); // Bass (tight): depth .95, attack 1, hold 25, release 130
        head.ppq = 0.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 2.0, tone (110.0f), &audio, &head);
        const auto ducked = peakBetween (audio, 1.003, 1.02);
        const auto recovered = peakBetween (audio, 1.25, 1.45);
        check (ducked < 0.1f, "deep duck just after the beat (" + juce::String (ducked, 3) + ")");
        check (recovered > 0.48f, "fully recovered mid-beat (" + juce::String (recovered, 3) + ")");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("kybalion_duck.wav"), audio);

        set (*p, Params::Duck::nudge, -20.0f);
        head.ppq = 0.0;
        render (*p, 2.0, tone (110.0f), &audio, &head);
        check (peakBetween (audio, 0.985, 0.995) < 0.15f, "negative nudge ducks before the beat");

        set (*p, Params::Duck::breakdown, 1.0f);
        head.ppq = 0.0;
        render (*p, 2.0, tone (110.0f), &audio, &head);
        check (peakBetween (audio, 1.003, 1.02) > 0.48f, "Breakdown switch turns ducking off");
        p->setPlayHead (nullptr);
    }

    section ("Polarity: low-band split leaves the top untouched");
    {
        auto p = make (false, true);
        p->applyDuckPreset (5); // Lows only, split 180 Hz
        head.ppq = 0.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 2.0, [] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto v = (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * t)
                                      + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * t));
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        }, &audio, &head);
        p->setPlayHead (nullptr);
        const auto lowDucked = bandLevel (audio, 50.0f, 1.0, 1.04), lowOpen = bandLevel (audio, 50.0f, 1.3, 1.46);
        const auto highDucked = bandLevel (audio, 3000.0f, 1.003, 1.04), highOpen = bandLevel (audio, 3000.0f, 1.3, 1.46);
        check (lowDucked < 0.35f * lowOpen, "50 Hz is ducked (" + juce::String (juce::Decibels::gainToDecibels (lowDucked / lowOpen), 1) + " dB)");
        check (highDucked > 0.85f * highOpen, "3 kHz is not (" + juce::String (juce::Decibels::gainToDecibels (highDucked / highOpen), 1) + " dB)");
    }

    section ("Polarity: sidechain input triggers on kicks");
    {
        auto p = std::make_unique<KybalionProcessor>();
        set (*p, Params::gateOn, 0.0f);
        set (*p, Params::Duck::source, 1.0f);
        p->enableAllBuses();                       // main stereo + stereo sidechain
        p->setRateAndBufferSizeDetails (sampleRate, blockSize);
        p->prepareToPlay (sampleRate, blockSize);
        const auto channels = p->getTotalNumInputChannels();
        check (channels == 4, "sidechain bus enabled (" + juce::String (channels) + " input channels)");
        if (channels != 4) return finish();

        // Kick at 0.3 s and 0.9 s on the sidechain; steady tone on the main input.
        const auto total = (int) (1.4 * sampleRate);
        juce::AudioBuffer<float> out (2, total);
        juce::AudioBuffer<float> block (channels, blockSize);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < total; pos += blockSize)
        {
            const auto n = std::min (blockSize, total - pos);
            block.setSize (channels, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto v = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t);
                block.setSample (0, i, v); block.setSample (1, i, v);
                float kick = 0.0f;
                for (double k : { 0.3, 0.9 })
                    if (t >= k && t < k + 0.08) kick = (float) (0.9 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * (t - k)) * std::exp (-(t - k) * 30.0));
                block.setSample (2, i, kick); block.setSample (3, i, kick);
            }
            p->processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, block, ch, 0, n);
        }
        check (peakBetween (out, 0.1, 0.29) > 0.48f, "no ducking before the first kick");
        check (peakBetween (out, 0.31, 0.33) < 0.15f, "ducks on the first kick");
        check (peakBetween (out, 0.7, 0.88) > 0.48f, "recovers between kicks");
        check (peakBetween (out, 0.91, 0.93) < 0.15f, "ducks again on the second kick");
    }

    section ("Gate fills before drops and breakdowns");
    {
        // 120 BPM (2 s bars): Groove bars 1-2, Drop 3-4, Groove 5-6, Breakdown 7-8.
        suite::SectionMap map;
        map.totalBars = 8; map.count = 4;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 3, suite::SectionType::Drop };
        map.sections[2] = { 5, suite::SectionType::Groove };
        map.sections[3] = { 7, suite::SectionType::Breakdown };
        auto run = [&map] (int when, int style)
        {
            auto p = make (true, false);
            for (int i = 0; i < Params::numSteps; ++i) set (*p, Params::Gate::step (i), 1.0f);   // gate wide open
            set (*p, Params::Gate::length, 1.0f);
            set (*p, Params::Gate::filter, 0.0f);
            set (*p, Params::Fill::when, (float) when);
            set (*p, Params::Fill::style, (float) style);
            suite::SectionBus::publish (map);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> out;
            render (*p, 13.0, tone (220.0f), &out, &head);
            p->setPlayHead (nullptr);
            suite::SectionBus::withdraw();
            return out;
        };

        const auto gap = run (Params::Fill::BeforeDrops, TranceGate::Gap);
        check (channelRms (gap, 0, 3.55, 3.95) < 0.05f * channelRms (gap, 0, 2.2, 3.4), "Gap: silence on the last beat before the drop");
        check (channelRms (gap, 0, 2.2, 3.4) > 0.3f && channelRms (gap, 0, 4.2, 5.0) > 0.3f, "open before it and in the drop");
        check (channelRms (gap, 0, 11.55, 11.95) > 0.3f, "'Before drops' leaves the breakdown alone");

        const auto stutter = run (Params::Fill::BeforeDrops, TranceGate::Stutter);
        // 1/32 notes at 120 BPM are 62.5 ms: open for the first 45%, closed after.
        const auto open = channelRms (stutter, 0, 3.0 + 0.005, 3.0 + 0.022), shut = channelRms (stutter, 0, 3.0 + 0.040, 3.0 + 0.060);
        check (open > 0.25f && shut < 0.3f * open, "Stutter: 1/32 chops in the fill bar (" + juce::String (open, 3) + " / " + juce::String (shut, 3) + ")");
        check (channelRms (stutter, 0, 1.0 + 0.040, 1.0 + 0.060) > 0.3f, "and only in the fill bar");

        const auto autoFills = run (Params::Fill::BeforeDropsAndBreakdowns, 0);
        const auto halfOn = channelRms (autoFills, 0, 10.03, 10.22), halfOff = channelRms (autoFills, 0, 10.28, 10.48);   // beats are 0.5 s
        check (halfOn > 0.3f && halfOff < 0.1f * halfOn, "Auto: half-time fill into the breakdown");
        // The first drop gets a roll: by beat 3 of the fill bar it's at 1/16 (on for 62.5 ms, off for 62.5 ms).
        check (channelRms (autoFills, 0, 3.0 + 0.075, 3.0 + 0.12) < 0.3f * channelRms (autoFills, 0, 3.0 + 0.005, 3.0 + 0.055),
               "Auto: a roll into the drop");

        const auto off = run (Params::Fill::Off, 0);
        check (channelRms (off, 0, 3.55, 3.95) > 0.3f && channelRms (off, 0, 10.55, 10.95) > 0.3f, "Fills off: the pattern plays through");
    }

    section ("Fit to kick");
    {
        // A synthetic trance kick at 138 BPM: a pitch sweep down to `bodyHz`, ringing ~200 ms to -30 dB.
        auto kickRun = [] (double bodyHz)
        {
            auto p = std::make_unique<KybalionProcessor>();
            set (*p, Params::gateOn, 0.0f);   // beat-grid ducking: the kick is analysed whenever it's connected
            p->enableAllBuses();
            p->setRateAndBufferSizeDetails (sampleRate, blockSize);
            p->prepareToPlay (sampleRate, blockSize);
            const auto channels = p->getTotalNumInputChannels();
            juce::AudioBuffer<float> block (channels, blockSize);
            juce::MidiBuffer midi;
            const auto beat = 60.0 / 138.0;
            double phase = 0.0;
            const auto total = (int) (3.0 * sampleRate);
            for (int pos = 0; pos < total; pos += blockSize)
            {
                const auto n = std::min (blockSize, total - pos);
                block.setSize (channels, n, false, false, true);
                block.clear();
                for (int i = 0; i < n; ++i)
                {
                    const auto t = (double) (pos + i) / sampleRate;
                    const auto k = std::fmod (t, beat);
                    if (k < 1.0 / sampleRate) phase = 0.0;
                    const auto hz = bodyHz + 95.0 * std::exp (-k / 0.008);
                    phase += juce::MathConstants<double>::twoPi * hz / sampleRate;
                    const auto v = (float) (0.9 * std::sin (phase) * std::exp (-k / 0.06));
                    if (channels >= 4) { block.setSample (2, i, v); block.setSample (3, i, v); }
                }
                p->processBlock (block, midi);
            }
            return p;
        };

        auto p = kickRun (55.0);
        check (p->kick.hits() >= 5, "hears the kicks (" + juce::String (p->kick.hits()) + ")");
        const auto len = p->kick.lengthMs(), hz = p->kick.pitchHz();
        check (len > 160.0f && len < 260.0f, "measures the kick's length (" + juce::String (len, 0) + " ms, expected about 200)");
        check (hz > 53.0f && hz < 60.0f, "measures the body pitch (" + juce::String (hz, 1) + " Hz, expected 55-57)");

        check (p->fitToKick(), "Fit to kick applies");
        const auto hold = p->apvts.getRawParameterValue (Params::Duck::hold)->load();
        const auto release = p->apvts.getRawParameterValue (Params::Duck::release)->load();
        check (std::abs (hold - 0.15f * len) < 2.0f && std::abs (release - 0.85f * len) < 3.0f,
               "hold and release follow the kick (" + juce::String (hold, 0) + " / " + juce::String (release, 0) + " ms)");
        {
            // The learning pass sends the same as a fix, undoable.
            suite::FixBus::reset();
            p->apvts.getParameter (Params::Duck::hold)->setValueNotifyingHost (0.0f);
            const auto seq = suite::FixBus::send (suite::Fix::KickFit);
            p->fixes->poll();
            const auto learned = p->apvts.getRawParameterValue (Params::Duck::hold)->load();
            suite::FixBus::sendUndo (seq);
            p->fixes->poll();
            check (std::abs (learned - 0.15f * len) < 2.0f && p->apvts.getRawParameterValue (Params::Duck::hold)->load() < 0.5f,
                   "the learning pass's kick fit does the same, and undoes");
        }

        const auto inTune = describeKickTuning (hz, 9);
        check (inTune.contains ("in tune with the root"), "an A kick in A minor is in tune: " + inTune);
        const auto off = describeKickTuning (61.7f, 9);
        check (off.contains ("2.0 st above the root (A)") && off.contains ("tune the kick down 2.0 st"), "a B kick in A is flagged: " + off);
        check (describeKickTuning (82.4f, 9).contains ("on the fifth"), "an E kick in A is on the fifth");

        auto none = std::make_unique<KybalionProcessor>();
        none->prepareToPlay (sampleRate, blockSize);
        check (! none->fitToKick(), "no sidechain, no kicks: nothing changes");
    }

    section ("Duck to the suite kick (no routing)");
    {
        suite::KickBus::withdraw();
        const auto beat = 60.0 / 138.0;
        // Kicks on the offbeats (an unusual pattern, so it can't be the beat grid by accident).
        InputFn offbeatKick = [beat] (juce::AudioBuffer<float>& b, int64_t pos)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) (pos + i) / sampleRate;
                const auto k = std::fmod (t + 0.5 * beat, beat);   // hits at 0.5, 1.5, 2.5, 3.5 beats
                const auto v = (float) (0.9 * std::sin (juce::MathConstants<double>::twoPi * 55.0 * k) * std::exp (-k / 0.05));
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
        };

        auto kickTrack = make (false, false);
        set (*kickTrack, Params::Duck::shareKick, 1.0f);
        FakePlayHead head;
        kickTrack->setPlayHead (&head);
        render (*kickTrack, 4.0 * 4.0 * beat + 0.05, offbeatKick, nullptr, &head);   // 4 bars
        kickTrack->setPlayHead (nullptr);
        const auto& learned = kickTrack->kickLearner.patterns().bySection[(size_t) suite::KickPatternSet::anySection];
        const auto bodyHz = kickTrack->kickLearner.patterns().bodyHz;
        check (std::abs (bodyHz - 55.0f) < 5.0f, "the kick track also shares the kick's body pitch, for Azoth's impact (" + juce::String (bodyHz, 1) + " Hz)");
        bool positionsRight = learned.count == 4;
        for (int i = 0; positionsRight && i < 4; ++i)
            positionsRight = std::abs (learned.positions[(size_t) i] - (0.5f + (float) i)) < 0.03f;
        check (positionsRight, "the kick track learns its pattern (" + juce::String (learned.count) + " hits: "
                   + juce::String (learned.positions[0], 2) + ", " + juce::String (learned.positions[1], 2) + " ...)");
        check (suite::KickBus::read().valid, "and shares it with the suite");

        auto pad = make (false, true);
        set (*pad, Params::Duck::source, (float) Params::Duck::SuiteKick);
        head.ppq = 0.0;
        pad->setPlayHead (&head);
        juce::AudioBuffer<float> out;
        render (*pad, 2.0 * 4.0 * beat, tone (220.0f), &out, &head);
        pad->setPlayHead (nullptr);
        // Ducked just after each offbeat kick, open again just before the next.
        const auto bar2 = 4.0 * beat;
        const auto justAfter = channelRms (out, 0, bar2 + 1.5 * beat + 0.005, bar2 + 1.5 * beat + 0.03);
        const auto before = channelRms (out, 0, bar2 + 1.5 * beat - 0.05, bar2 + 1.5 * beat - 0.01);
        const auto onBeat = channelRms (out, 0, bar2 + 2.0 * beat + 0.005, bar2 + 2.0 * beat + 0.03);
        check (justAfter < 0.5f * before, "another track ducks right after each shared kick (" + juce::String (justAfter, 3)
                   + " vs " + juce::String (before, 3) + ")");
        check (onBeat > justAfter * 1.5f, "... and not on the beat grid, where there's no kick");
        check (pad->suiteKickAvailable.load(), "reports the suite kick as available");

        suite::KickBus::withdraw();
        auto alone = make (false, true);
        set (*alone, Params::Duck::source, (float) Params::Duck::SuiteKick);
        head.ppq = 0.0;
        alone->setPlayHead (&head);
        render (*alone, 4.0 * beat, tone (220.0f), &out, &head);
        alone->setPlayHead (nullptr);
        check (! alone->suiteKickAvailable.load() && channelRms (out, 0, beat + 0.005, beat + 0.03) < 0.5f * channelRms (out, 0, beat - 0.05, beat - 0.01),
               "no shared kick yet: falls back to ducking on the beat grid");

        // Auto-role: on a track called "Kick", Kybalion shares the kick and doesn't duck itself.
        auto named = make (false, true);
        juce::AudioProcessor::TrackProperties props;
        props.name = juce::String ("Kick 1");
        named->updateTrackProperties (props);
        check (named->apvts.getRawParameterValue (Params::Duck::shareKick)->load() > 0.5f
                   && named->apvts.getRawParameterValue (Params::duckOn)->load() < 0.5f, "a track named 'Kick' shares its kick automatically");
        check (named->getTrackName() == "Kick 1", "knows its track name");
    }

    section ("Ouroboros: no ducking in breakdowns and builds");
    {
        suite::SectionMap map;   // Groove 1-2, Breakdown 3-4, Drop 5-6 (120 BPM: 2 s per bar)
        map.totalBars = 6; map.count = 3;
        map.sections[0] = { 1, suite::SectionType::Groove };
        map.sections[1] = { 3, suite::SectionType::Breakdown };
        map.sections[2] = { 5, suite::SectionType::Drop };

        auto p = make (false, true);
        p->applyDuckPreset (0);
        suite::SectionBus::publish (map);
        head.ppq = 0.0;
        p->setPlayHead (&head);
        juce::AudioBuffer<float> audio;
        render (*p, 11.0, tone (110.0f), &audio, &head);
        p->setPlayHead (nullptr);
        suite::SectionBus::withdraw();
        check (peakBetween (audio, 2.003, 2.02) < 0.1f, "ducks on the beat in the Groove");
        check (peakBetween (audio, 5.003, 5.02) > 0.48f, "no ducking on the beat in the Breakdown");
        check (peakBetween (audio, 9.003, 9.02) < 0.1f, "ducking returns in the Drop");
    }

    section ("Gate on Correspondence's chord changes");
    {
        suite::Registry::reset();
        suite::Registry::announce (77, "Correspondence", "Chords", 0, { 0.0f, 4.0f, 0.0f, 0.0f });   // a chord every bar
        auto run = [] (int mode, int firstOpen)
        {
            auto p = make (true, false);
            for (int i = 0; i < Params::numSteps; ++i) set (*p, Params::Gate::step (i), i >= firstOpen ? 1.0f : 0.0f);
            set (*p, Params::Gate::depth, 1.0f);
            set (*p, Params::Gate::swing, 0.0f);
            set (*p, Params::Gate::stereo, 0.0f);
            set (*p, Params::Fill::chords, (float) mode);
            p->suiteStatus();   // reads the chord length from the suite
            FakePlayHead head;
            head.bpm = 120.0;   // a bar every 2 s
            p->setPlayHead (&head);
            juce::AudioBuffer<float> audio;
            render (*p, 6.2, tone (440.0f), &audio, &head);
            p->setPlayHead (nullptr);
            return std::make_pair (channelRms (audio, 0, 4.01, 4.1), p->chordAccents.load());
        };
        const auto [off, offCount] = run (Params::Fill::ChordsOff, 1);      // step 1 (the downbeat) closed
        const auto [accent, accents] = run (Params::Fill::ChordsAccent, 1);
        check (off < 0.01f && accent > 0.2f && accents == 4, "accent: each chord change opens the gate on its downbeat ("
                   + juce::String (off, 3) + " -> " + juce::String (accent, 3) + ", " + juce::String (accents) + " changes)");
        suite::Registry::announce (77, "Correspondence", "Chords", 0, { 0.0f, 2.0f, 0.0f, 0.0f });   // half-bar chords
        auto quiet = [] (int mode)
        {
            auto p = make (true, false);
            for (int i = 0; i < Params::numSteps; ++i) set (*p, Params::Gate::step (i), i >= 8 ? 1.0f : 0.0f);
            set (*p, Params::Gate::depth, 1.0f);
            set (*p, Params::Gate::swing, 0.0f);
            set (*p, Params::Fill::chords, (float) mode);
            p->suiteStatus();
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            juce::AudioBuffer<float> audio;
            render (*p, 4.0, tone (440.0f), &audio, &head);
            p->setPlayHead (nullptr);
            return channelRms (audio, 0, 1.0, 4.0);
        };
        check (quiet (Params::Fill::ChordsRestart) < 0.01f && quiet (Params::Fill::ChordsOff) > 0.1f,
               "restart: with half-bar chords the pattern starts again at every change, so its second half never plays");
        suite::Registry::reset();
    }

    section ("Dynamic gate");
    {
        suite::SectionMap map;   // 120 BPM: Drop bars 1-8 (0-16 s)
        map.totalBars = 8; map.count = 1;
        map.sections[0] = { 1, suite::SectionType::Drop };
        auto run = [&] (bool dynamic, double seconds)
        {
            auto p = make (true, false);
            suite::SectionBus::publish (map);
            set (*p, Params::Gate::dynamic, dynamic ? 1.0f : 0.0f);
            set (*p, suite::SceneEngine::enabledId, 0.0f);
            FakePlayHead head;
            head.bpm = 120.0;
            p->setPlayHead (&head);
            render (*p, seconds, tone (440.0f), nullptr, &head);
            p->setPlayHead (nullptr);
            return p->dynamicDepth.load();
        };
        const auto early = run (true, 1.0), late = run (true, 15.0);
        check (early < 0.7f && late > 0.95f, "through the drop the gate deepens from about 60% to full depth ("
                   + juce::String (early, 2) + " -> " + juce::String (late, 2) + ")");
        suite::SectionBus::withdraw();
    }

    section ("Kick and bass phase check");
    {
        auto run = [] (bool invertedBass, bool flip)
        {
            auto p = std::make_unique<KybalionProcessor>();
            set (*p, Params::gateOn, 0.0f);
            set (*p, Params::duckOn, 0.0f);
            set (*p, Params::Duck::flip, flip ? 1.0f : 0.0f);
            p->enableAllBuses();
            p->setRateAndBufferSizeDetails (sampleRate, blockSize);
            p->prepareToPlay (sampleRate, blockSize);
            const auto channels = p->getTotalNumInputChannels();
            juce::AudioBuffer<float> block (channels, blockSize);
            juce::MidiBuffer midi;
            const auto total = (int) (2.0 * sampleRate);
            for (int pos = 0; pos < total; pos += blockSize)
            {
                const auto n = std::min (blockSize, total - pos);
                block.setSize (channels, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    const auto t = (double) (pos + i) / sampleRate, beat = std::fmod (t, 0.5);
                    const auto kick = 0.6f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * beat) * std::exp (-beat * 10.0));
                    const auto bass = (invertedBass ? -0.4f : 0.4f) * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * beat) * std::exp (-beat * 10.0));
                    block.setSample (0, i, bass); block.setSample (1, i, bass);
                    if (channels >= 4) { block.setSample (2, i, kick); block.setSample (3, i, kick); }
                }
                p->processBlock (block, midi);
            }
            return std::make_pair (p->phaseMeasured.load(), p->lowCorrelation.load());
        };
        const auto [seen, cancelling] = run (true, false);
        const auto [seen2, fixed] = run (true, true);
        const auto [seen3, together] = run (false, false);
        check (seen && cancelling < -0.8f && fixed > 0.8f && together > 0.8f,
               "a bass that cancels the kick reads out of phase (" + juce::String (cancelling, 2) + "); Flip polarity fixes it (" + juce::String (fixed, 2) + ")");
    }

    section ("Your gate patterns");
    {
        using S = KybalionProcessor::Steps;
        S offbeat {}, trancey {}, padsOnly {};
        for (int i = 0; i < Params::numSteps; ++i)
        {
            offbeat[(size_t) i] = i % 4 == 2 ? 1.0f : 0.0f;
            trancey[(size_t) i] = i % 2 == 0 ? 1.0f : 0.3f;
            padsOnly[(size_t) i] = i % 3 == 0 ? 1.0f : 0.0f;
        }
        check (KybalionProcessor::stepsFromText (KybalionProcessor::stepsText (trancey)) == trancey, "a pattern round-trips as text");
        check (KybalionProcessor::trackKind ("Big Pad 2") == "pads" && KybalionProcessor::trackKind ("Lead Saw") == "leads"
                   && KybalionProcessor::trackKind ("Vox Chops") == "vocals" && KybalionProcessor::trackKind ("FX") == "other",
               "the kind of track comes from its name");

        // Play a song to its final drop with the gate on: the pattern is logged once.
        {
            suite::SectionMap m;
            m.totalBars = 4; m.count = 2;
            m.sections[0] = { 1, suite::SectionType::Build };
            m.sections[1] = { 3, suite::SectionType::Drop };
            auto p = make (true, false);
            suite::SectionBus::publish (m);   // after make(), which clears any map
            p->applySteps (offbeat);
            p->trackNameChanged ("Pluck 1");
            FakePlayHead h;
            h.bpm = 120.0;
            p->setPlayHead (&h);
            render (*p, 2.0, tone (220.0f), nullptr, &h);
            p->suiteStatus();
            const auto before = suite::StyleLog::read().size();
            render (*p, 2.5, tone (220.0f), nullptr, &h);   // into the drop (bar 3, 4 s)
            p->setPlayHead (nullptr);
            p->suiteStatus();
            p->suiteStatus();
            const auto entries = suite::StyleLog::read();
            check (before == 0 && entries.size() == 1 && entries[0].get ("steps") == KybalionProcessor::stepsText (offbeat) && entries[0].get ("track") == "plucks",
                   "the pattern you kept is logged when the song reaches its final drop, once, with the kind of track");
            suite::SectionBus::withdraw();
        }
        check (! KybalionProcessor().startedOnFavourite, "one song isn't enough to change a new Kybalion");
        suite::StyleLog::add ("gate", { { "steps", KybalionProcessor::stepsText (offbeat) }, { "track", "plucks" } });
        suite::StyleLog::add ("gate", { { "steps", KybalionProcessor::stepsText (trancey) }, { "track", "leads" } });
        suite::StyleLog::add ("gate", { { "steps", KybalionProcessor::stepsText (padsOnly) }, { "track", "pads" } });
        suite::StyleLog::add ("gate", { { "steps", KybalionProcessor::stepsText (padsOnly) }, { "track", "pads" } });
        const auto favs = KybalionProcessor::favouritePatterns (suite::StyleLog::read());
        check (favs.size() == 3 && favs[0].first == offbeat && favs[0].second == 2, "your most used pattern comes first (kept in 2 songs)");
        KybalionProcessor fresh;
        check (fresh.startedOnFavourite && fresh.currentSteps() == offbeat, "a new Kybalion starts on it");
        fresh.trackNameChanged ("Lush Pads");
        check (fresh.currentSteps() == padsOnly, "and on a pad track, on your favourite for pads");
        fresh.applySteps (trancey);
        fresh.trackNameChanged ("Pluck bus");
        check (fresh.currentSteps() == trancey, "but never over a pattern you chose");
        testStyleLog.deleteFile();
    }

    section ("Role-aware setup");
    {
        auto named = [] (suite::SuiteProcessor& p, const char* name)
        {
            juce::AudioProcessor::TrackProperties props;
            props.name = juce::String (name);
            p.updateTrackProperties (props);
        };
        suite::Registry::reset();
        KybalionProcessor bass;
        bass.captureFreshValues();
        named (bass, "Sub Bass");
        check (bass.tryRoleSetup() && (int) bass.apvts.getRawParameterValue (Params::Duck::source)->load() == Params::Duck::SuiteKick
                   && bass.apvts.getRawParameterValue (Params::duckOn)->load() > 0.5f && bass.apvts.getRawParameterValue (Params::gateOn)->load() < 0.5f,
               "a new Kybalion on 'Sub Bass' ducks tight to the suite kick, no gate");
        KybalionProcessor pads;
        pads.captureFreshValues();
        named (pads, "Big Pads");
        check (pads.tryRoleSetup() && pads.apvts.getRawParameterValue (Params::gateOn)->load() > 0.5f, "on 'Big Pads' it gates and ducks deep");
        KybalionProcessor touched;
        touched.captureFreshValues();
        touched.apvts.getParameter (Params::Duck::depth)->setValueNotifyingHost (0.1f);
        named (touched, "Sub Bass");
        check (! touched.tryRoleSetup() && touched.roleSetupApplied < 0, "but never over one you've already set up");
        KybalionProcessor fromRole;
        fromRole.captureFreshValues();
        named (fromRole, "Insert 7");
        suite::Registry::announce (777, "Prima Materia", "Insert 7", suite::Status::roleSet, { 2.0f, 0, 0, 0 });
        check (fromRole.tryRoleSetup() && fromRole.roleSetupApplied == suite::roleBass, "a track named 'Insert 7' takes the role its Prima Materia was given");
        suite::Registry::reset();
    }

    section ("Presets and state");
    {
        auto p = make (true, true);
        p->applyGatePattern (2); // 3-3-2
        check (std::abs (p->apvts.getRawParameterValue (Params::Gate::step (3))->load() - 1.0f) < 1.0e-6f
               && p->apvts.getRawParameterValue (Params::Gate::step (1))->load() < 1.0e-6f, "3-3-2 pattern loads");

        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = make (true, true);
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (q->apvts.getRawParameterValue (Params::Gate::step (3))->load() - 1.0f) < 1.0e-6f, "gate steps restored");

        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KybalionPatterns");
        folder.deleteRecursively();
        set (*p, Params::Gate::step (5), 0.5f);
        check (p->saveGatePattern (folder, "My/groove"), "a pattern saves as a file");
        const auto mine = KybalionProcessor::loadGatePatterns (folder);
        check (mine.size() == 1 && std::abs (mine[0].second[5] - 0.5f) < 1.0e-3f && std::abs (mine[0].second[3] - 1.0f) < 1.0e-3f,
               "and loads back with its steps (" + (mine.empty() ? juce::String ("none") : mine[0].first) + ")");
        auto r = make (true, true);
        if (! mine.empty()) r->applySteps (mine[0].second);
        check (std::abs (r->apvts.getRawParameterValue (Params::Gate::step (5))->load() - 0.5f) < 1.0e-3f, "and applies in another project");
        folder.deleteRecursively();
    }

    if (outDir != juce::File())
    {
        auto p = make (true, true);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        auto* tabs = dynamic_cast<suite::ui::TabBar*> (editor->findChildWithID ("tabs"));
        snapshot (*editor, outDir.getChildFile ("kybalion_rhythm.png"));
        if (tabs != nullptr) tabs->setSelected (1);
        snapshot (*editor, outDir.getChildFile ("kybalion_polarity.png"));
        if (tabs != nullptr) tabs->setSelected (2);
        snapshot (*editor, outDir.getChildFile ("kybalion_scenes.png"));
        editor.reset();
    }

    return finish();
}
