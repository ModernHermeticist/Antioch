// Offline tests for Kybalion. Usage: KybalionTests [output-dir]

#include "PluginProcessor.h"
#include "SuiteUI.h"
#include "SectionBus.h"
#include "TestUtils.h"

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
    }

    if (outDir != juce::File())
    {
        auto p = make (true, true);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        auto* tabs = dynamic_cast<suite::ui::TabBar*> (editor->findChildWithID ("tabs"));
        snapshot (*editor, outDir.getChildFile ("kybalion_rhythm.png"));
        if (tabs != nullptr) tabs->setSelected (1);
        snapshot (*editor, outDir.getChildFile ("kybalion_polarity.png"));
        editor.reset();
    }

    return finish();
}
